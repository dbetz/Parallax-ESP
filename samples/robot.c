/*
  Robot test program
*/
#include "simpletools.h"
#include "abdrive.h"
#include "ping.h"
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

#define PING_PIN    10

#define DEBUG

fdserial *wifi;
fdserial *debug;

int wheelLeft;
int wheelRight;

void init_robot(void);
int process_robot_command(int whichWay);            
void set_robot_speed(int left, int right);

int main(void)
{    
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
    
    init_robot();
    
//    request("SET:sscp-pause-time,5");
    nrequest(SSCP_TKN_SET, "sscp-pause-time,5");
    waitFor(SSCP_PREFIX "=S,0\r");

    request("LISTEN:0,/robot*");
    waitFor(SSCP_PREFIX "=S,0\r");
    
    for (;;) {
        char type[16], verb[128], size[128], url[128], arg[128];
        int chan;

        waitcnt(CNT + CLKFREQ/4);

        request("POLL");
        waitFor(SSCP_PREFIX "=");
        collectUntil(',', type, sizeof(type));
        if (type[0] != 'N')
            dprint(debug, "Got %c\n", type[0]);
        
        switch (type[0]) {
        case 'P':
            collectUntil(',', arg, sizeof(arg));
            chan = atoi(arg);
            collectUntil('\r', size, sizeof(size));
            dprint(debug, "%d: POST size %d\n", chan, atoi(size));
            request("PATH:%d", chan);
            waitFor(SSCP_PREFIX "=");
            collectUntil(',', type, sizeof(type));
            collectUntil('\r', url, sizeof(url));
            dprint(debug, "%d: path '%s'\n", chan, url);
            if (strcmp(url, "/robot") == 0) {
                request("POSTARG:%d,gto", chan);
                waitFor(SSCP_PREFIX "=S,");
                collectUntil('\r', arg, sizeof(arg));
                dprint(debug, "gto='%s'\n", arg);
                if (process_robot_command(arg[0]) != 0)
                    dprint(debug, "Unknown robot command: '%c'\n", arg[0]);
                reply(chan, 200, "OK");
                waitFor(SSCP_PREFIX "=S,0\r");
            }
            else {
                dprint(debug, "Unknown POST URL\n");
                reply(chan, 400, "unknown");
                waitFor(SSCP_PREFIX "=S,0\r");
            }
            break;
        case 'G':
            collectUntil(',', arg, sizeof(arg));
            chan = atoi(arg);
            collectUntil('\r', size, sizeof(size));
            dprint(debug, "%d: GET size %d\n", chan, atoi(size));
            request("PATH:%d", chan);
            waitFor(SSCP_PREFIX "=");
            collectUntil(',', type, sizeof(type));
            collectUntil('\r', url, sizeof(url));
            dprint(debug, "%d: path '%s'\n", chan, url);
            if (strcmp(url, "/robot-ping") == 0) {
                sprintf(arg, "%d", ping_cm(PING_PIN));
                reply(chan, 200, arg);
                waitFor(SSCP_PREFIX "=S,0\r");
            }
            else {
                dprint(debug, "Unknown POST URL\n");
                request("REPLY:%d,400,unknown", chan);
                waitFor(SSCP_PREFIX "=S,0\r");
            }
            break;
        case 'N':
            skipUntil('\r');
            break;
        default:
            skipUntil('\r');
            dprint(debug, "unknown response: '%c' 0x%02x\n", type[0], type[0]);
            break;
        }
    }
    
    return 0;
}

void init_robot(void)
{
  wheelLeft = wheelRight = 0;
  high(26);
  set_robot_speed(wheelLeft, wheelRight);
}

int process_robot_command(int whichWay)            
{ 
  toggle(26);
      
  switch (whichWay) {
  
  case 'F': // forward
    #ifdef DEBUG
      dprint(debug, "Forward\n");
    #endif
    if (wheelLeft > wheelRight)
      wheelRight = wheelLeft;
    else if (wheelLeft < wheelRight) 
      wheelLeft = wheelRight;
    else {           
      wheelLeft = wheelLeft + 32;
      wheelRight = wheelRight + 32;
    }      
    break;    
    
  case 'R': // right
    #ifdef DEBUG
      dprint(debug, "Right\n");
    #endif
    wheelLeft = wheelLeft + 16;
    wheelRight = wheelRight - 16;
    break;
    
  case 'L': // left
    #ifdef DEBUG
      dprint(debug, "Left\n");
    #endif
    wheelLeft = wheelLeft - 16;
    wheelRight = wheelRight + 16;
    break;
    
  case 'B': // reverse
    #ifdef DEBUG
      dprint(debug, "Reverse\n");
    #endif
    if(wheelLeft < wheelRight)
      wheelRight = wheelLeft;
    else if (wheelLeft > wheelRight) 
      wheelLeft = wheelRight;
    else {           
      wheelLeft = wheelLeft - 32;
      wheelRight = wheelRight - 32;
    }
    break;  
        
  case 'S': // stop
    #ifdef DEBUG
      dprint(debug, "Stop\n");
    #endif
    wheelLeft = 0;
    wheelRight = 0;
    break;
    
  default:  // unknown request
    return -1;
  }    
  
  if (wheelLeft > 128) wheelLeft = 128;
  if (wheelLeft < -128) wheelLeft = -128;
  if (wheelRight > 128) wheelRight = 128;
  if (wheelRight < -128) wheelRight = -128;
  
  set_robot_speed(wheelLeft, wheelRight);
    
  return 0;
}

void set_robot_speed(int left, int right)
{  
  #ifdef DEBUG
    dprint(debug, "L %d, R %d\n", wheelLeft, wheelRight);
  #endif
  
  wheelLeft = left;
  wheelRight = right;
  drive_speed(wheelLeft, wheelRight);
}
