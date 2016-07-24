/*
  Robot test program
*/
#include "simpletools.h"
#include "abdrive.h"
#include "ping.h"
#include "cmd.h"

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

#define PING_PIN    10

#define DEBUG

fdserial *debug;

int wheelLeft;
int wheelRight;

void init_robot(void);
int process_robot_command(int whichWay);            
void set_robot_speed(int left, int right);

#define LONG_REPLY  "This is a very long reply that should be broken into multiple chunks to test REPLY followed by SEND.\r\n"

void handleEvent(wifi *esp, char type, char handle, int listener);

int main(void)
{    
    int listenHandle;
    wifi *esp;
    
    // Close default same-cog terminal
    simpleterm_close();                         

    esp = wifi_open(WIFI_RX, WIFI_TX);
#ifdef SEPARATE_DEBUG_PINS
    debug = fdserial_open(DEBUG_RX, DEBUG_TX, 0, 115200);
#else
    debug = esp->port;
#endif

    dbg("\n\nRobot Firmware 2.0\n");
    
    init_robot();
    
    wifi_setInteger(esp, "cmd-events", 1);
    wifi_listenHTTP(esp, "/robot*", &listenHandle);
    
    for (;;) {
        int handle, listener;
        char type;
        
        if (wifi_checkForEvent(esp, &type, &handle, &listener) > 0) {
            dbg("Got event %c: handle %d, listener %d\n", type, handle, listener);
            handleEvent(esp, type, handle, listener);
        }
    }
    
    return 0;
}

void handleEvent(wifi *esp, char type, char handle, int listener)
{
    char url[128], arg[128];

    switch (type) {
    case 'P':
        if (listener == 0)
            dbg("%d: disconnected\n", handle);
        else {
            wifi_path(esp, handle, url, sizeof(url));
            dbg("%d: path '%s'\n", handle, url);
            if (strcmp(url, "/robot") == 0) {
                wifi_arg(esp, handle, "gto", arg, sizeof(arg));
                dbg("gto='%s'\n", arg);
                if (process_robot_command(arg[0]) != 0)
                    dbg("Unknown robot command: '%c'\n", arg[0]);
                wifi_reply(esp, handle, 200, "");
            }
            else {
                dbg("Unknown POST URL\n");
                wifi_reply(esp, handle, 404, "unknown");
            }
        }
        break;
    case 'G':
        if (listener == 0)
            dbg("%d: disconnected\n", handle);
        else {
            wifi_path(esp, handle, url, sizeof(url));
            dbg("%d: path '%s'\n", handle, url);
            if (strcmp(url, "/robot-ping") == 0) {
                sprintf(arg, "%d", ping_cm(PING_PIN));
                wifi_reply(esp, handle, 200, arg);
            }
            else if (strcmp(url, "/robot-test") == 0) {
                wifi_reply(esp, handle, 200, LONG_REPLY);
            }
            else {
                dbg("Unknown GET URL\n");
                wifi_reply(esp, handle, 404, "unknown");
            }
        }
        break;
    default:
        dbg("unknown event: '%c' 0x%02x\n", type, type);
        break;
    }
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
      dbg("Forward\n");
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
      dbg("Right\n");
    #endif
    wheelLeft = wheelLeft + 16;
    wheelRight = wheelRight - 16;
    break;
    
  case 'L': // left
    #ifdef DEBUG
      dbg("Left\n");
    #endif
    wheelLeft = wheelLeft - 16;
    wheelRight = wheelRight + 16;
    break;
    
  case 'B': // reverse
    #ifdef DEBUG
      dbg("Reverse\n");
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
      dbg("Stop\n");
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
    dbg("L %d, R %d\n", wheelLeft, wheelRight);
  #endif
  
  wheelLeft = left;
  wheelRight = right;
  drive_speed(wheelLeft, wheelRight);
}
