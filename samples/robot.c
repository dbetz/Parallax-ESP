/*
  Robot test program
*/
#include "simpletools.h"
#include "abdrive.h"
#include "ping.h"

// uncomment this if the wifi module is on pins other than 31/30
//#define SEPARATE_WIFI_PINS

#ifdef SEPARATE_WIFI_PINS
#define WIFI_RX     9
#define WIFI_TX     8
#else
#define WIFI_RX     31
#define WIFI_TX     30
#endif

#define SSCP_PREFIX "\xFE"
#define SSCP_START  0xFE

#define PING_PIN    10

#define DEBUG

fdserial *wifi;
fdserial *debug;

int wheelLeft;
int wheelRight;

void init_robot(void);
int process_robot_command(int whichWay);            
void set_robot_speed(int left, int right);

void request(char *fmt, ...);
int waitFor(char *target);
void collectUntil(int term, char *buf, int size);
void skipUntil(int term);

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

    request("LISTEN,0,/robot*");
    waitFor(SSCP_PREFIX "=OK\r");
    
    for (;;) {
        char type[16], verb[128], url[128], arg[128];
        int chan;

        waitcnt(CNT + CLKFREQ/4);

        request("POLL");
        waitFor(SSCP_PREFIX "=");
        collectUntil(':', type, sizeof(type));
        if (type[0] != 'N')
            dprint(debug, "Got %c\n", type[0]);
        
        switch (type[0]) {
#if 0
        case 'H':
            collectUntil(',', arg, sizeof(arg));
            chan = atoi(arg);
            collectUntil(',', verb, sizeof(verb));
            collectUntil('\r', url, sizeof(url));
            
            if (verb[0]) {
                dprint(debug, "%d: VERB '%s', URL '%s'\n", chan, verb, url);
                if (strcmp(verb, "POST") == 0 && strcmp(url, "/robot") == 0) {
                    request("POSTARG,%d,gto", chan);
                    waitFor(SSCP_PREFIX "=");
                    collectUntil('\r', arg, sizeof(arg));
                    dprint(debug, "gto='%s'\n", arg);
                    if (process_robot_command(arg[0]) != 0)
                        dprint(debug, "Unknown robot command: '%c'\n", arg[0]);
                    request("REPLY,%d,200,OK", chan);
                    waitFor(SSCP_PREFIX "=OK\r");
                }
                else {
                    dprint(debug, "Unknown command\n");
                }
            }
            break;
#else
        case 'P':
            collectUntil(',', arg, sizeof(arg));
            chan = atoi(arg);
            collectUntil('\r', url, sizeof(url));
            dprint(debug, "%d: URL '%s'\n", chan, url);
            if (strcmp(url, "/robot") == 0) {
                request("POSTARG,%d,gto", chan);
                waitFor(SSCP_PREFIX "=");
                collectUntil('\r', arg, sizeof(arg));
                dprint(debug, "gto='%s'\n", arg);
                if (process_robot_command(arg[0]) != 0)
                    dprint(debug, "Unknown robot command: '%c'\n", arg[0]);
                request("REPLY,%d,200,OK", chan);
                waitFor(SSCP_PREFIX "=OK\r");
            }
            else {
                dprint(debug, "Unknown POST URL\n");
                request("REPLY,%d,400,unknown", chan);
                waitFor(SSCP_PREFIX "=OK\r");
            }
            break;
        case 'G':
            collectUntil(',', arg, sizeof(arg));
            chan = atoi(arg);
            collectUntil('\r', url, sizeof(url));
            dprint(debug, "%d: URL '%s'\n", chan, url);
            if (strcmp(url, "/robot-ping") == 0) {
                request("REPLY,%d,200,%d", chan, ping_cm(PING_PIN));
                waitFor(SSCP_PREFIX "=OK\r");
            }
            else {
                dprint(debug, "Unknown POST URL\n");
                request("REPLY,%d,400,unknown", chan);
                dprint(debug, "Unknown GET URL\n");
            }
            break;
#endif
        case 'N':
            skipUntil('\r');
            break;
        default:
            skipUntil('\r');
            dprint(debug, "unknown response\n");
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

void request(char *fmt, ...)
{
    char buf[100], *p = buf;
    va_list ap;
    va_start(ap, fmt);
    fdserial_txChar(wifi, SSCP_START);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    while (*p != '\0')
        fdserial_txChar(wifi, *p++);
    fdserial_txChar(wifi, '\n');
    va_end(ap);
}

int waitFor(char *target)
{
    int len = strlen(target);
    char buf[16];
    int ch, i;
    
    if (len > sizeof(buf))
        return -1;
        
    for (i = 0; i < len; ++i) {
        if ((ch = fdserial_rxChar(wifi)) == EOF)
            return -1;
        buf[i] = ch;
    }
        
    while (strncmp(target, buf, len) != 0) {
        memcpy(buf, &buf[1], len - 1);
        if ((ch = fdserial_rxChar(wifi)) == EOF)
            return -1;
        buf[len - 1] = ch;
    }
    
    return 0;
}

void collectUntil(int term, char *buf, int size)
{
    int ch, i;
    i = 0;
    while ((ch = fdserial_rxChar(wifi)) != EOF && ch != term) {
        if (i < size - 1)
            buf[i++] = ch;
    }
    buf[i] = '\0';
}

void skipUntil(int term)
{
    int ch;
    while ((ch = fdserial_rxChar(wifi)) != EOF && ch != term)
        ;
}
