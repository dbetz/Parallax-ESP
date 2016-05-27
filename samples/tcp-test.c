/*
  TCP test program
*/
#include "simpletools.h"
#include "fdserial.h"

// uncomment this if the wifi module is on pins other than 31/30
//#define SEPARATE_WIFI_PINS

#ifdef SEPARATE_WIFI_PINS
#define WIFI_RX     9
#define WIFI_TX     8
#else
#define WIFI_RX     31
#define WIFI_TX     30
#endif

#define SSCP_PREFIX "\xFE"
#define SSCP_START  0xFE

#define DEBUG

fdserial *wifi;
fdserial *debug;

void request(char *fmt, ...);
void requestPayload(char *buf, int len);
int waitFor(char *target);
void collectUntil(int term, char *buf, int size);
void collectPayload(char *buf, int bufSize, int count);
void skipUntil(int term);

int main(void)
{    
    char buf[1000];
    int chan, i;
    
    // Close default same-cog terminal
    simpleterm_close();                         

    // Set to open collector instead of driven
    wifi = fdserial_open(WIFI_RX, WIFI_TX, 0b0100, 115200);

    // Generate a BREAK to enter SSCP command mode
    pause(10);
    low(WIFI_TX);
    pause(1);
    input(WIFI_TX);
    pause(1);

#ifdef SEPARATE_WIFI_PINS
    debug = fdserial_open(31, 30, 0, 115200);
#else
    debug = wifi;
#endif
    
    for (;;) {
        char result[2];

        request("TCP-CONNECT,www-eng-x.llnl.gov,80");
        waitFor(SSCP_PREFIX "=");
        collectUntil(',', result, sizeof(result));
        collectUntil('\r', buf, sizeof(buf));
        chan = atoi(buf);
        dprint(debug, "Connect returned '%s,%d'\n", result, chan);
    
        if (result[0] == 'S') {
            dprint(debug, "Connected on channel %d\n", chan);

#define REQ "\
GET /documents/a_document.txt HTTP/1.1\r\n\
Host: www-eng-x.llnl.gov\r\n\
\r\n"

            request("TCP-SEND,%d,%d", chan, strlen(REQ));
            requestPayload(REQ, strlen(REQ));
            waitFor(SSCP_PREFIX "=");
            collectUntil('\r', buf, sizeof(buf));
            dprint(debug, "Send returned '%s'\n", buf);

            if (buf[0] == 'S') {
                int retries = 10;
                while (--retries >= 0) {
                    int count;

                    request("RECV,%d", chan);
                    waitFor(SSCP_PREFIX "=");
                    collectUntil(',', result, sizeof(result));
                    collectUntil('\r', buf, sizeof(buf));
                    count = atoi(buf);
                    collectPayload(buf, sizeof(buf), count);
                    if (count >= sizeof(buf))
                        count = sizeof(buf) - 1;
                    buf[count] = '\0';
                    dprint(debug, "Recv returned '%s,%d'\n", result, count);
                    dprint(debug, "%s", buf);
                    dprint(debug, "[EOF]\n");
    
                    if (result[0] == 'S')
                        break;

                    waitcnt(CNT + CLKFREQ/4);
                }
            }
    
            request("TCP-DISCONNECT,%d", chan);
            waitFor(SSCP_PREFIX "=");
            collectUntil('\r', buf, sizeof(buf));
            dprint(debug, "Disconnect returned '%s'\n", buf);
        }
    
        waitcnt(CNT + CLKFREQ/4);
    }
    
    dprintf(debug, "Done!\n");
    
    return 0;
}

void request(char *fmt, ...)
{
    char buf[100], *p = buf;
    va_list ap;
    va_start(ap, fmt);
    fdserial_txChar(wifi, SSCP_START);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    while (*p != '\0')
        fdserial_txChar(wifi, *p++);
    fdserial_txChar(wifi, '\n');
    va_end(ap);
}

void requestPayload(char *buf, int len)
{
    while (--len >= 0)
        fdserial_txChar(wifi, *buf++);
}

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
