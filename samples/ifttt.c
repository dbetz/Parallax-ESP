/*
  TCP test program
*/
#include "simpletools.h"
#include "fdserial.h"
#include "cmd.h"

//#define IfTTT_KEY   "YOUR_API_KEY"
#define IfTTT_KEY   "csY3T4PPMydBKXuaDVi72j"

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
    int type, chan;
    
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
    
    request("CONNECT:maker.ifttt.com,80");
    waitFor(SSCP_PREFIX "=^c,^i\r", &type, &chan);
    dprint(debug, "Connect returned '%c,%d'\n", type, chan);

    if (type == 'S') {
        dprint(debug, "Connected on channel %d\n", chan);

#define REQ "\
POST /trigger/post_tweet/with/key/" IfTTT_KEY " HTTP/1.1\r\n\
Host: maker.ifttt.com\r\n\
Connection: keep-alive\r\n\
Accept: */*\r\n\
\r\n"

        request("SEND:%d,%d", chan, strlen(REQ));
        requestPayload(REQ, strlen(REQ));
        waitFor(SSCP_PREFIX "=^s\r", buf, sizeof(buf));
        dprint(debug, "Send returned '%s'\n", buf);

        if (buf[0] == 'S') {
            int retries = 10;
            while (--retries >= 0) {
                int count, i;

                request("RECV:%d,%d", chan, sizeof(buf));
                waitFor(SSCP_PREFIX "=^c,^i\r", &type, &count);
                collectPayload(buf, sizeof(buf), count);
                if (count >= sizeof(buf))
                    count = sizeof(buf) - 1;
                buf[count] = '\0';
                dprint(debug, "Recv returned '%c,%d'\n", type, count);
                for (i = 0; i < count; ++i)
                    dprint(debug, "%c", buf[i]);
                dprint(debug, "[EOF]\n");

                if (type == 'S')
                    break;

                waitcnt(CNT + CLKFREQ/4);
            }
        }

        request("TCPDISCONNECT:%d", chan);
        waitFor(SSCP_PREFIX "=^s\r", buf, sizeof(buf));
        dprint(debug, "Disconnect returned '%s'\n", buf);
    }
    
    dprint(debug, "Done!\n");
    
    return 0;
}
