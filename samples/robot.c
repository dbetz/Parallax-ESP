/*
  Robot test program
*/
#include "simpletools.h"
#include "abdrive.h"
#include "ping.h"

// uncomment these if the wifi module is on pins other than 31/30
//#define WIFI_RX     9
//#define WIFI_TX     8

#define PING_PIN    10

#define DEBUG

fdserial *wifi;
fdserial *debug;

int wheelLeft;
int wheelRight;

void init_robot(void);
int process_robot_command(int whichWay);            
void set_robot_speed(int left, int right);

void request(char *req);
int waitFor(char *target);
void collectUntil(int term, char *buf, int size);

int main(void)
{    
    simpleterm_close();                         // Close default same-cog terminal
    debug = fdserial_open(31, 30, 0, 115200);

#ifdef WIFI_RX
    wifi = fdserial_open(WIFI_RX, WIFI_TX, 0, 115200);
#else
    wifi = debug;
#endif
    
    init_robot();

    request("LISTEN,0,/robot*");
    waitFor("$=OK\r");
    
    for (;;) {
        char verb[128], url[128], arg[128];
        waitcnt(CNT + CLKFREQ/4);
        request("POLL,0");
        waitFor("$=");
        collectUntil(',', verb, sizeof(verb));
        collectUntil('\r', url, sizeof(url));
        if (verb[0]) {
            dprint(debug, "VERB '%s', URL '%s'\n", verb, url);
            if (strcmp(verb, "POST") == 0 && strcmp(url, "/robot") == 0) {
                request("POSTARG,0,gto");
                waitFor("$=");
                collectUntil('\r', arg, sizeof(arg));
                dprint(debug, "gto='%s'\n", arg);
                if (process_robot_command(arg[0]) != 0)
                  dprint(debug, "Unknown robot command: '%c'\n", arg[0]);
                request("REPLY,0,200,OK");
                waitFor("$=OK\r");
            }
            else if (strcmp(verb, "GET") == 0 && strcmp(url, "/robot-ping") == 0) {
                int cmDist = ping_cm(PING_PIN);
                char buf[128];
                sprintf(buf, "REPLY,0,200,%d", cmDist);
                request(buf);
                waitFor("$=OK\r");
            }
            else {
                dprint(debug, "Unknown command\n");
            }
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

void request(char *req)
{
    fdserial_txChar(wifi, '$');
    while (*req)
        fdserial_txChar(wifi, *req++);
    fdserial_txChar(wifi, '\n');
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
