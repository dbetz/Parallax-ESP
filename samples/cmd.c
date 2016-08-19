#include "simpletools.h"
#include "fdserial.h"
#include "cmd.h"

// this is needed until vsprint gets added to Simple Libraries
//#define vsprint(buf, fmt, args) _intsprnt((fmt), (args), (buf))
#define vsprint(buf, fmt, args) vsprintf((buf), (fmt), (args))

static int checkForEvent(wifi *dev, char *buf, int maxSize);
static void request(wifi *dev, const char *fmt, ...);
static void requestPayload(wifi *dev, const char *buf, int len);
static int getResponse(wifi *dev, char *buf, int maxSize);
static int parseResponse(wifi *dev, const char *fmt, ...);
static int parseBuffer(const char *buf, const char *fmt, ...);
static int parseBuffer1(const char *buf, const char *fmt, va_list ap);

wifi *wifi_open(int wifi_rx, int wifi_tx)
{
    wifi *dev;
    if (!(dev = (wifi *)malloc(sizeof(wifi))))
        return NULL;
    if (wifi_init(dev, wifi_rx, wifi_tx) != 0) {
        free(dev);
        return NULL;
    }
    return dev;
}

int wifi_init(wifi *dev, int wifi_rx, int wifi_tx)
{
    // initialize the wifi device structure
    memset(dev, 0, sizeof(wifi));
    dev->messageState = WIFI_STATE_START;
    
    // set to open collector instead of driven
    if (!(dev->port = fdserial_open(wifi_rx, wifi_tx, 0b0100, 115200)))
        return -1;
        
    // generate a BREAK to enter CMD command mode
    pause(10);
    low(wifi_tx);
    pause(1);
    input(wifi_tx);
    pause(1);
        
    return 0;
}

int wifi_setInteger(wifi *dev, const char *name, int value)
{
    char result;
    int arg;
    request(dev, "SET:cmd-events,%d", value);
    if (parseResponse(dev, CMD_PREFIX "=^c,^i\r", &result, &arg) != 0 || result != 'S')
        return -1;
    return 0;
}

int wifi_listen(wifi *dev, char *protocol, const char *path, int *pHandle)
{
    char result;
    int arg;
    request(dev, "LISTEN:%s,%s", protocol, path);
    if (parseResponse(dev, CMD_PREFIX "=^c,^d\r", &result, &arg) != 0 || result != 'S')
        return -1;
    *pHandle = arg;
    return 0;
}

int wifi_path(wifi *dev, int handle, char *path, int maxSize)
{
    char result;
    request(dev, "PATH:%d", handle);
    if (parseResponse(dev, CMD_PREFIX "=^c,^s\r", &result, path, maxSize) != 0 || result != 'S')
        return -1;
    return 0;
}

int wifi_arg(wifi *dev, int handle, const char *name, char *buf, int maxSize)
{
    char result;
    request(dev, "ARG:%d,%s", handle, name);
    if (parseResponse(dev, CMD_PREFIX "=^c,^s\r", &result, buf, maxSize) != 0 || result != 'S')
        return -1;
    return 0;
}

int wifi_checkForEvent(wifi *dev, char *pType, int *pHandle, int *pListener)
{
    char buf[128];
    
    if (checkForEvent(dev, buf, sizeof(buf)) <= 0)
        return 0;
        
    if (parseBuffer(buf, CMD_PREFIX "!^c,^i,^i\r", pType, pHandle, pListener) != 0)
        return -1;
        
    return 1;
}

static int checkForMessage(wifi *dev, int type, char *buf, int maxSize)
{
    int ch, newTail, i, j;
    
    while ((ch = fdserial_rxCheck(dev->port)) != -1) {
        switch (dev->messageState) {
        case WIFI_STATE_START:
            if (ch == CMD_START) {
                dev->messageStart = dev->messageTail;
                newTail = (dev->messageTail + 1) % sizeof(dev->messageBuffer);
                if (newTail != dev->messageHead) {
                    dev->messageBuffer[dev->messageTail] = ch;
                    dev->messageTail = newTail;
                }
                else {
                    dbg("MSG: queue overflow\n");
                    return -1;
                }
                dev->messageState = WIFI_STATE_DATA;
            }
            else {
                // out of band data
                return -1;
            }
            break;
        case WIFI_STATE_DATA:
            newTail = (dev->messageTail + 1) % sizeof(dev->messageBuffer);
            if (newTail != dev->messageHead) {
                dev->messageBuffer[dev->messageTail] = ch;
                dev->messageTail = newTail;
            }
            else {
                dbg("MSG: queue overflow\n");
                return -1;
            }
            if (ch == '\r') {
#if 1
                {   char tmp[128];
                    i = dev->messageStart;
                    j = 0;
                    while (i != dev->messageTail) {
                        if (j < sizeof(tmp) - 1)
                            tmp[j++] = dev->messageBuffer[i];
                        i = (i + 1) % sizeof(dev->messageBuffer);
                    }
                    tmp[j] = '\0';
                    dbg("Got: %s\n", &tmp[1]);
                }
#endif
                dev->messageState = WIFI_STATE_START;
                i = (dev->messageStart + 1) % sizeof(dev->messageBuffer);
                if (dev->messageBuffer[i] == type) {
                    i = dev->messageStart;
                    j = 0;
                    while (i != dev->messageTail) {
                        if (j < maxSize - 1)
                            buf[j++] = dev->messageBuffer[i];
                        i = (i + 1) % sizeof(dev->messageBuffer);
                    }
                    buf[j] = '\0';
                    dev->messageTail = dev->messageStart;
                    return j;
                }
            }
            break;
        default:
            dbg("MSG: internal error\n");
            return -1;
        }
    }

    return 0;
}

static int getResponse(wifi *dev, char *buf, int maxSize)
{
    int ret;
    while ((ret = checkForMessage(dev, '=', buf, maxSize)) <= 0)
        ;
    return ret;
}

static int checkForEvent(wifi *dev, char *buf, int maxSize)
{
    if (dev->messageHead != dev->messageTail) {
        int i = dev->messageHead;
        int j = 0;
        int ch;
        while (i != dev->messageTail) {
            ch = dev->messageBuffer[i];
            if (j < maxSize - 1)
                buf[j++] = ch;
            i = (i + 1) % sizeof(dev->messageBuffer);
            if (ch == '\r') {
                dev->messageHead = 0;
                buf[j] = '\0';
                dbg("Got queued event: %s\n", &buf[1]);
                return j;
            }   
        }
    }
    return checkForMessage(dev, '!', buf, maxSize);
}

static void request(wifi *dev, const char *fmt, ...)
{
    char buf[100], *p = buf;
    
    va_list ap;
    va_start(ap, fmt);
    vsprint(buf, fmt, ap);
    va_end(ap);
    
    fdserial_txChar(dev->port, CMD_START);
    while (*p != '\0')
        fdserial_txChar(dev->port, *p++);
    fdserial_txChar(dev->port, '\r');
}

static void requestPayload(wifi *dev, const char *buf, int len)
{
    while (--len >= 0)
        fdserial_txChar(dev->port, *buf++);
}

#define CHUNK_SIZE  8

int wifi_reply(wifi *dev, int chan, int code, const char *payload)
{
    int payloadLength = (payload ? strlen(payload) : 0);
    char result;
    int count;
    
    if (payloadLength == 0) {
dbg("REPLY %d, %d\n", chan, code);
        request(dev, "REPLY:%d,%d", chan, code);
    }
        
    else {
        int remaining = payloadLength;
        while (remaining > 0) {
            if ((count = remaining) > CHUNK_SIZE)
                count = CHUNK_SIZE;
            if (remaining == payloadLength) {
dbg("REPLY %d, %d, %d, %d\n", chan, code, payloadLength, count);
                request(dev, "REPLY:%d,%d,%d,%d", chan, code, payloadLength, count);
                requestPayload(dev, payload, count);
            }
            else {
dbg("SEND %d, %d\n", chan, count);
                request(dev, "SEND:%d,%d", chan, count);
                requestPayload(dev, payload, count);
            }
            payload += count;
            remaining -= count;
            if (remaining > 0) {
                parseResponse(dev, CMD_PREFIX "=^c,^d\r", &result, &count);
dbg(" ret %c %d\n", result, count);
                if (result != 'S') {
dbg(" failed with %d\n", count);
                    return count;
                }
            }
        }
    }
    
    parseResponse(dev, CMD_PREFIX "=^c,^d\r", &result, &count);
dbg(" final ret %c %d\n", result, count);
    
    return payloadLength;
}

static int parseResponse(wifi *dev, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    int ret;
    
    if (getResponse(dev, buf, sizeof(buf)) <= 0)
        return -1;
        
    va_start(ap, fmt);
    ret = parseBuffer1(buf, fmt, ap);
    va_end(ap);
    
    return ret;
}

static void wifi_collectPayload(wifi *dev, char *buf, int bufSize, int count)
{
    while (--count >= 0) {
        int ch = fdserial_rxChar(dev->port);
        if (bufSize > 0) {
            *buf++ = ch;
            --bufSize;
        }
    }
}

typedef struct {
    const char *p;
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

static int parseBuffer(const char *buf, const char *fmt, ...)
{
    va_list ap;
    int ret;
    
    va_start(ap, fmt);
    ret = parseBuffer1(buf, fmt, ap);
    va_end(ap);
    
    return ret;
}

static int parseBuffer1(const char *buf, const char *fmt, va_list ap)
{
    State state = { .p = buf, .savedChar = EOF };
    const char *p = fmt;
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
