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

int main(void)
{    
    char buf[1000];
    int type, handle;
    
    cmd_init(WIFI_RX, WIFI_TX, 31, 30);

    request("CONNECT:maker.ifttt.com,80");
    waitFor(CMD_PREFIX "=^c,^i\r", &type, &handle);
    dprint(debug, "Connect returned '%c,%d'\n", type, handle);

    if (type == 'S') {
        dprint(debug, "Connected on handle %d\n", handle);

#define REQ "\
POST /trigger/post_tweet/with/key/" IfTTT_KEY " HTTP/1.1\r\n\
Host: maker.ifttt.com\r\n\
Connection: keep-alive\r\n\
Accept: */*\r\n\
\r\n"

        request("SEND:%d,%d", handle, strlen(REQ));
        requestPayload(REQ, strlen(REQ));
        waitFor(CMD_PREFIX "=^s\r", buf, sizeof(buf));
        dprint(debug, "Send returned '%s'\n", buf);

        if (buf[0] == 'S') {
            int retries = 10;
            while (--retries >= 0) {
                int count, i;

                request("RECV:%d,%d", handle, sizeof(buf));
                waitFor(CMD_PREFIX "=^c,^i\r", &type, &count);
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

        request("CLOSE:%d", handle);
        waitFor(CMD_PREFIX "=^s\r", buf, sizeof(buf));
        dprint(debug, "Disconnect returned '%s'\n", buf);
    }
    
    dprint(debug, "Done!\n");
    
    return 0;
}
