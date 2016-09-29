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
    int blink = 0;
    
    cmd_init(WIFI_RX, WIFI_TX, 31, 30);

    pause(500);
    
    int value;
    request("CHECK:pin-gpio15");
    waitFor(CMD_START "=S,^i\r", &value); 
    dprint(debug, "value = %d\n", value); 
    
    for (;;) {
        waitcnt(CNT + CLKFREQ/2);
        request("SET:pin-gpio15,%d", blink);
        waitFor(CMD_START "=S,0\r");
        blink = !blink;
    }
    
    return 0;
}
