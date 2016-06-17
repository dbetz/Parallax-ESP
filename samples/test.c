#include "simpletools.h"
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
    int handle1, handle2, handle3, err, tries;
    char result1, result2, result3;
    char cmd[] = { CMD_TKN_LISTEN, CMD_TKN_HTTP, '/', 't', 'h', 'i', 's', '\r', 0 };
    
    cmd_init(WIFI_RX, WIFI_TX, 31, 30);

    tries = 3;
    while (--tries >= 0) {

        request(cmd);
        //request("LISTEN:HTTP,/this");
        waitFor(CMD_PREFIX "=^c,^i\r", &result1, &handle1);
        dprint(debug, "'LISTEN:HTTP,/this' returned %c, %d\n", result1, handle1);
    
        request("LISTEN:HTTP,/that");
        waitFor(CMD_PREFIX "=^c,^i\r", &result2, &handle2);
        dprint(debug, "'LISTEN:HTTP,/that' returned %c, %d\n", result2, handle2);
    
        request("LISTEN:HTTP,/trouble");
        waitFor(CMD_PREFIX "=^c,^i\r", &result3, &handle3);
        dprint(debug, "'LISTEN:HTTP,/trouble' returned %c, %d\n", result3, handle3);

        if (result1 == 'S') {
            request("CLOSE:%d", handle1);
            waitFor(CMD_PREFIX "=^c,^i\r", &result1, &err);
            dprint(debug, "'CLOSE:%d' returned %c, %d\n", handle1, result1, err);
        }

        if (result2 == 'S') {
            request("CLOSE:%d", handle2);
            waitFor(CMD_PREFIX "=^c,^i\r", &result2, &err);
            dprint(debug, "'CLOSE:%d' returned %c, %d\n", handle2, result2, err);
        }
    
        if (result3 == 'S') {
            dprint(debug, "Didn't expect to have to close handle3\n");
        }
    }
        
    return 0;
}
