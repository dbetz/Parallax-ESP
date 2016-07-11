#include "simpletools.h"
#include "fdserial.h"
#include "cmd.h"

fdserial *wifi;
fdserial *debug;

enum {
    STATE_START,
    STATE_DATA
};

static int messageState;
static char messageBuffer[1024];
static int messageHead;
static int messageTail;
static int messageStart;

static int parseBuffer1(char *buf, char *fmt, va_list ap);

void cmd_init(int wifi_rx, int wifi_tx, int debug_rx, int debug_tx)
{
    // Close default same-cog terminal
    simpleterm_close();                         

    // Set to open collector instead of driven
    wifi = fdserial_open(wifi_rx, wifi_tx, 0b0100, 115200);

    // Generate a BREAK to enter CMD command mode
    pause(10);
    low(wifi_tx);
    pause(1);
    input(wifi_tx);
    pause(1);

    if (debug_tx != -1 && wifi_tx != debug_tx)
        debug = fdserial_open(debug_rx, debug_tx, 0, 115200);
    else
        debug = wifi;
        
    messageHead = messageTail = 0;
    messageState = STATE_START;
}

void request(char *fmt, ...)
{
    char buf[100], *p = buf;
    
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    
    fdserial_txChar(wifi, CMD_START);
    while (*p != '\0')
        fdserial_txChar(wifi, *p++);
    fdserial_txChar(wifi, '\r');
}

void nrequest(int token, char *fmt, ...)
{
    char buf[100], *p = buf;
    
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    
    fdserial_txChar(wifi, CMD_START);
    fdserial_txChar(wifi, token);
    while (*p != '\0')
        fdserial_txChar(wifi, *p++);
    fdserial_txChar(wifi, '\r');
}

void requestPayload(char *buf, int len)
{
    while (--len >= 0)
        fdserial_txChar(wifi, *buf++);
}

#define CHUNK_SIZE  8

static int reply1(int chan, int code, char *payload)
{
    int payloadLength = (payload ? strlen(payload) : 0);
    char result;
    int count;
    
    if (payloadLength == 0) {
dprint(debug, "REPLY %d, %d\n", chan, code);
        request("REPLY:%d,%d", chan, code);
    }
        
    else {
        int remaining = payloadLength;
        while (remaining > 0) {
            if ((count = remaining) > CHUNK_SIZE)
                count = CHUNK_SIZE;
            if (remaining == payloadLength) {
dprint(debug, "REPLY %d, %d, %d, %d\n", chan, code, payloadLength, count);
                request("REPLY:%d,%d,%d,%d", chan, code, payloadLength, count);
                requestPayload(payload, count);
            }
            else {
dprint(debug, "SEND %d, %d\n", chan, count);
                request("SEND:%d,%d", chan, count);
                requestPayload(payload, count);
            }
            payload += count;
            remaining -= count;
            if (remaining > 0) {
                parseResponse(CMD_PREFIX "=^c,^d\r", &result, &count);
dprint(debug, " ret %c %d\n", result, count);
                if (result != 'S') {
dprint(debug, " failed with %d\n", count);
                    return count;
                }
            }
        }
    }
    
    parseResponse(CMD_PREFIX "=^c,^d\r", &result, &count);
dprint(debug, " final ret %c %d\n", result, count);
    
    return payloadLength;
}

int reply(int chan, int code, char *payload)
{
    int ret = reply1(chan, code, payload);
    dprint(debug, "ret: %d\n", ret);
    return ret;
}

int checkForMessage(int type, char *buf, int maxSize)
{
    int ch, newTail, i, j;
    
    while ((ch = fdserial_rxCheck(wifi)) != -1) {
        switch (messageState) {
        case STATE_START:
            if (ch == CMD_START) {
                messageStart = messageTail;
                newTail = (messageTail + 1) % sizeof(messageBuffer);
                if (newTail != messageHead) {
                    messageBuffer[messageTail] = ch;
                    messageTail = newTail;
                }
                else {
                    dprint(debug, "MSG: queue overflow\n");
                    return -1;
                }
                messageState = STATE_DATA;
            }
            else {
                // out of band data
                return -1;
            }
            break;
        case STATE_DATA:
            newTail = (messageTail + 1) % sizeof(messageBuffer);
            if (newTail != messageHead) {
                messageBuffer[messageTail] = ch;
                messageTail = newTail;
            }
            else {
                dprint(debug, "MSG: queue overflow\n");
                return -1;
            }
            if (ch == '\r') {
                {   char tmp[128];
                    i = messageStart;
                    j = 0;
                    while (i != messageTail) {
                        if (messageBuffer[i] != '\r' && j < sizeof(tmp) - 1)
                            tmp[j++] = messageBuffer[i];
                        i = (i + 1) % sizeof(messageBuffer);
                    }
                    tmp[j] = '\0';
                    dprint(debug, "Got: '%s'\n", &tmp[1]);
                }
                messageState = STATE_START;
                i = (messageStart + 1) % sizeof(messageBuffer);
                if (messageBuffer[i] == type) {
                    i = messageStart;
                    j = 0;
                    while (i != messageTail) {
                        if (j < maxSize - 1)
                            buf[j++] = messageBuffer[i];
                        i = (i + 1) % sizeof(messageBuffer);
                    }
                    buf[j] = '\0';
                    messageTail = messageStart;
                    return j;
                }
            }
            break;
        default:
            dprint(debug, "MSG: internal error\n");
            return -1;
        }
    }
        
    return 0;
}

int checkForEvent(char *buf, int maxSize)
{
    if (messageHead != messageTail) {
        int j = 0;
        int ch;
        while (messageHead != messageTail) {
            ch = messageBuffer[messageHead];
            if (j < maxSize - 1)
                buf[j++] = ch;
            messageHead = (messageHead + 1) % sizeof(messageBuffer);
        } while (ch != '\r');
        buf[j] = '\0';
        return j;           
    }
    return checkForMessage('!', buf, maxSize);
}

int getResponse(char *buf, int maxSize)
{
    int ret;
    while ((ret = checkForMessage('=', buf, maxSize)) <= 0)
        ;
    return ret;
}

typedef struct {
    char *p;
    int savedChar;
} State;

static int nextchar(State *state)
{
    int ch;
    if ((ch = state->savedChar) != EOF)
        state->savedChar = EOF;
    else
        ch = *state->p ? *state->p++ : EOF;
    return ch;
}

static void ungetchar(State *state, int ch)
{
    state->savedChar = ch;
}

int parseBuffer(char *buf, char *fmt, ...)
{
    va_list ap;
    int ret;
    
    va_start(ap, fmt);
    ret = parseBuffer1(buf, fmt, ap);
    va_end(ap);
    
    return ret;
}

int parseResponse(char *fmt, ...)
{
    char buf[128];
    va_list ap;
    int ret;
    
    if (getResponse(buf, sizeof(buf)) <= 0)
        return -1;
        
    va_start(ap, fmt);
    ret = parseBuffer1(buf, fmt, ap);
    va_end(ap);
    
    return ret;
}

static int parseBuffer1(char *buf, char *fmt, va_list ap)
{
    State state = { .p = buf, .savedChar = EOF };
    char *p = fmt;
    int ch, rch;

    while ((ch = *p++) != '\0') {
    
        rch = nextchar(&state);
        
        if (ch == '^') {
            switch (*p++) {
            case 'c':
                if (rch == EOF)
                    return -1;
                *va_arg(ap, char *) = rch;
                break;
            case 'i':
                {
                    int value = 0;
                    int sign = 1;
                    if (rch == EOF)
                        return -1;
                    if (rch == '-') {
                        if ((rch = nextchar(&state)) == EOF)
                            return -1;
                        sign = -1;
                    }
                    if (!isdigit(rch))
                        return -1;
                    do {
                        value = value * 10 + rch - '0';
                    } while ((rch = nextchar(&state)) != EOF && isdigit(rch));
                    *va_arg(ap, int *) = value * sign;
                    ungetchar(&state, rch);
                }
                break;
            case 's':
                {
                    int term = *p;
                    char *buf = va_arg(ap, char *);
                    int len = va_arg(ap, int);
                    while (rch != EOF && rch != term) {
                        if (len > 1) {
                            *buf++ = rch;
                            --len;
                        }
                        rch = nextchar(&state);
                    }
                    *buf = '\0';
                    ungetchar(&state, rch);
                }
                break;
            case '^':
                if (rch != '^')
                    return -1;
                break;
            case '\0':
                // fall through
            default:
                return -1;
            }
        }
        
        else {
            if (rch != ch)
                return -1;
       }
    }

    return 0;
}

void collectPayload(char *buf, int bufSize, int count)
{
    while (--count >= 0) {
        int ch = fdserial_rxChar(wifi);
        if (bufSize > 0) {
            *buf++ = ch;
            --bufSize;
        }
    }
}
