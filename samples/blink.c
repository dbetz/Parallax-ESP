#include "simpletools.h"
#include "cmd.h"

fdserial *debug;

// uncomment this to use wifi pins other than 31/30
//#define SEPARATE_WIFI_PINS

#ifdef SEPARATE_WIFI_PINS
#define WIFI_RX    9
#define WIFI_TX    8
#else
#define WIFI_RX    31
#define WIFI_TX    30
#endif

// uncomment this to use debug pins other than 31/30
//#define SEPARATE_DEBUG_PINS

#ifdef SEPARATE_DEBUG_PINS
#define DEBUG_RX    9
#define DEBUG_TX    8
#else
#define DEBUG_RX    31
#define DEBUG_TX    30
#endif

int main(void)
{    
    int blink = 0;
    wifi *esp;
    
    // Close default same-cog terminal
    simpleterm_close();                         

    esp = wifi_open(WIFI_RX, WIFI_TX);
#ifdef SEPARATE_DEBUG_PINS
    debug = fdserial_open(DEBUG_RX, DEBUG_TX, 0, 115200);
#else
    debug = esp->port;
#endif

    pause(500);
    
    int value;
    wifi_getInteger(esp, "pin-gpio15", &value);
    dprint(debug, "value = %d\n", value); 
    
    for (;;) {
        waitcnt(CNT + CLKFREQ/2);
        wifi_setInteger(esp, "pin-gpio15", blink);
        blink = !blink;
    }
    
    return 0;
}
