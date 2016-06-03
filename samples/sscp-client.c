#include "simpletools.h"
#include "fdserial.h"
#include "sscp-client.h"

extern fdserial *wifi;

void request(char *fmt, ...)
{
    char buf[100], *p = buf;
    
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    
    fdserial_txChar(wifi, SSCP_START);
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
    
    fdserial_txChar(wifi, SSCP_START);
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

void reply(int chan, int code, char *payload)
{
    int payloadLength = strlen(payload);
    request("REPLY:%d,%d,%d", chan, code, payloadLength);
    requestPayload(payload, payloadLength);
}

typedef struct {
    int savedChar;
} State;

static int nextchar(State *state)
{
    int ch;
    if ((ch = state->savedChar) != EOF)
        state->savedChar = EOF;
    else
        ch = fdserial_rxChar(wifi);
    return ch;
}

static void ungetchar(State *state, int ch)
{
    state->savedChar = ch;
}

int waitFor(char *fmt, ...)
{
    State state = { .savedChar = EOF };
    int ch, rch, len, i;
    char *p = fmt;
    char buf[100];
    int ret = -1;
    
    len = 0;
    while (*p != '\0' && *p != '^') {
        ++len;
        ++p;
    }
        
    if (len > sizeof(buf))
        return -1;
        
    for (i = 0; i < len; ++i) {
        if ((rch = nextchar(&state)) == EOF)
            return -1;
        buf[i] = rch;
    }
        
    while (strncmp(fmt, buf, len) != 0) {
        memcpy(buf, &buf[1], len - 1);
        if ((rch = nextchar(&state)) == EOF)
            return -1;
        buf[len - 1] = rch;
    }
    
    va_list ap;
    va_start(ap, fmt);
    
    while ((ch = *p++) != '\0') {
    
        rch = nextchar(&state);
        
        if (ch == '^') {
            switch (*p++) {
            case 'c':
                if (rch == EOF)
                    goto done;
                *va_arg(ap, char *) = rch;
                break;
            case 'i':
                {
                    int value = 0;
                    int sign = 1;
                    if (rch == EOF)
                        goto done;
                    if (rch == '-') {
                        if ((rch = nextchar(&state)) == EOF)
                            goto done;
                        sign = -1;
                    }
                    if (!isdigit(rch))
                        goto done;
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
                    goto done;
                break;
            case '\0':
                // fall through
            default:
                goto done;
            }
        }
        
        else {
            if (rch != ch)
                goto done;
       }
    }

    ret = 0;
        
done:
    va_end(ap);
    
    return ret;
}

void collectUntil(int term, char *buf, int size)
{
    int ch, i;
    i = 0;
    while ((ch = fdserial_rxChar(wifi)) != EOF && ch != term) {
        if (i < size - 1)
            buf[i++] = ch;
    }
    buf[i] = '\0';
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

void skipUntil(int term)
{
    int ch;
    while ((ch = fdserial_rxChar(wifi)) != EOF && ch != term)
        ;
}
