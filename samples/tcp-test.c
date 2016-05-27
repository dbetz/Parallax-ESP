/*
  TCP test program
*/
#include "simpletools.h"
#include "fdserial.h"
#include "sscp-client.h"

// uncomment this if the wifi module is on pins other than 31/30
//#define SEPARATE_WIFI_PINS

#ifdef SEPARATE_WIFI_PINS
#define WIFI_RX     9
#define WIFI_TX     8
#else
#define WIFI_RX     31
#define WIFI_TX     30
#endif

#define DEBUG

fdserial *wifi;
fdserial *debug;

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

            request("SEND,%d,%d", chan, strlen(REQ));
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
                    dprint(debug, "%s[EOF]\n");
    
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
