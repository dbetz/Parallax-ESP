/*
  TCP test program
*/
#include "simpletools.h"
#include "fdserial.h"
#include "cmd.h"

// uncomment this if the wifi module is on pins other than 31/30
//#define SEPARATE_WIFI_PINS

#ifdef SEPARATE_WIFI_PINS
#define WIFI_RX     9
#define WIFI_TX     8
#else
#define WIFI_RX     31
#define WIFI_TX     30
#endif

int main(void)
{    
    char buf[1000];
    int chan, type;
    
    cmd_init(WIFI_RX, WIFI_TX, 31, 30);

    for (;;) {

        request("CONNECT:www-eng-x.llnl.gov,80");
        waitFor(SSCP_PREFIX "=^c,^i\r", &type, &handle);
        dprint(debug, "Connect returned '%c,%d'\n", type, handle);
    
        if (type == 'S') {
            dprint(debug, "Connected on handle %d\n", handle);

#define REQ "\
GET /documents/a_document.txt HTTP/1.1\r\n\
Host: www-eng-x.llnl.gov\r\n\
\r\n"

            request("SEND:%d,%d", handle, strlen(REQ));
            requestPayload(REQ, strlen(REQ));
            waitFor(SSCP_PREFIX "=^s\r", buf, sizeof(buf));
            dprint(debug, "Send returned '%s'\n", buf);

            if (buf[0] == 'S') {
                int retries = 10;
                while (--retries >= 0) {
                    int count, i;

                    request("RECV:%d,%d", handle, sizeof(buf));
                    waitFor(SSCP_PREFIX "=^c,^i\r", &type, &count);
                    collectPayload(buf, sizeof(buf), count);
                    dprint(debug, "Recv returned '%c,%d'\n", type, count);
                    if (count >= sizeof(buf))
                        count = sizeof(buf) - 1;
                    buf[count] = '\0';
                    for (i = 0; i < count; ++i)
                        dprint(debug, "%c", buf[i]);
                    dprint(debug, "[EOF]\n");
    
                    if (type == 'S')
                        break;

                    waitcnt(CNT + CLKFREQ/4);
                }
            }
    
            request("CLOSE:%d", handle);
            waitFor(SSCP_PREFIX "=^s\r", buf, sizeof(buf));
            dprint(debug, "Close returned '%s'\n", buf);
        }
    
        waitcnt(CNT + CLKFREQ/4);
    }
    
    dprint(debug, "Done!\n");
    
    return 0;
}
