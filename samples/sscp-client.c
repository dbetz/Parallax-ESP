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

/*
        waitFor(SSCP_PREFIX "=");
        collectUntil(',', type, sizeof(type));
        if (type[0] != 'N')
            dprint(debug, "Got %c\n", type[0]);
        
        switch (type[0]) {
        case 'P':
            collectUntil(',', arg, sizeof(arg));
            chan = atoi(arg);
            collectUntil('\r', size, sizeof(size));
    SSCP_PREFIX "=%c,%d,%d\r"
*/

int waitFor(char *target)
{
    int len = strlen(target);
    char buf[16];
    int ch, i;
    
    if (len > sizeof(buf))
        return -1;
        
    for (i = 0; i < len; ++i) {
        if ((ch = fdserial_rxChar(wifi)) == EOF)
            return -1;
        buf[i] = ch;
    }
        
    while (strncmp(target, buf, len) != 0) {
        memcpy(buf, &buf[1], len - 1);
        if ((ch = fdserial_rxChar(wifi)) == EOF)
            return -1;
        buf[len - 1] = ch;
    }
    
    return 0;
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
