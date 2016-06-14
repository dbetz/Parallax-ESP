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

fdserial *wifi;
fdserial *debug;

#define LONG_REPLY  "This is a very long reply that should be broken into multiple chunks to test REPLY followed by SEND.\r\n"

int main(void)
{    
    int blink = 0;
    
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
        waitcnt(CNT + CLKFREQ/2);
        request("SET:pin-gpio15,%d", blink);
        waitFor(SSCP_PREFIX "=S,0\r");
        blink = !blink;
    }
    
    return 0;
}
