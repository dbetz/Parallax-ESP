#ifndef __SSCP_H__
#define __SSCP_H__

#include "httpd.h"
#include "cgiwebsocket.h"

#define SSCP_PATH_MAX       32
#define SSCP_RX_BUFFER_MAX  1024

typedef struct Listener Listener;
typedef struct Connection Connection;

#define LISTENER_UNUSED     0
#define LISTENER_HTTP       1
#define LISTENER_WEBSOCKET  2

struct Listener {
    int type;
    char path[SSCP_PATH_MAX];
    Connection *connections;
};

#define CONNECTION_INIT     0x0001
#define CONNECTION_RXFULL   0x0002

struct Connection {
    int index;
    int flags;
    Listener *listener;
    char rxBuffer[SSCP_RX_BUFFER_MAX];
    void *data; // HttpConnData or Websock
    Connection *next;
};

int cgiSSCPHandleRequest(HttpdConnData *connData);
void sscp_init(void);
void sscp_enable(int enable);
int sscp_isEnabled(void);
void sscp_filter(char *buf, short len, void (*outOfBand)(void *data, char *buf, short len), void *data);
void sscp_websocketConnect(Websock *ws);

void sscp_sendResponse(char *fmt, ...);

// from sscp-tcp.c
void tcp_do_connect(int argc, char *argv[]);
void tcp_do_disconnect(int argc, char *argv[]);
void tcp_do_send(int argc, char *argv[]);
void tcp_do_recv(int argc, char *argv[]);

#endif

