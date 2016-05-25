#ifndef __SSCP_H__
#define __SSCP_H__

#include "httpd.h"
#include "cgiwebsocket.h"

#define SSCP_PATH_MAX       32
#define SSCP_RX_BUFFER_MAX  1024

typedef struct sscp_listener sscp_listener;
typedef struct sscp_connection sscp_connection;

#define LISTENER_UNUSED     0
#define LISTENER_HTTP       1
#define LISTENER_WEBSOCKET  2

struct sscp_listener {
    int type;
    char path[SSCP_PATH_MAX];
    sscp_connection *connections;
};

enum {
    CONNECTION_INIT     = 0x00000001
    CONNECTION_RXFULL   = 0x00000002
    CONNECTION_SENDING  = 0x00000004
};

enum {
    SSCP_STATE_IDLE = 0,
    SSCP_STATE_CONNECTING,
    SSCP_STATE_CONNECTED
};

struct sscp_connection {
    int index;
    int state;
    int flags;
    union {
        struct {
            sscp_listener *listener;
            sscp_connection *next;
            HttpConnData *conn;
        } http;
        struct {
            sscp_listener *listener;
            sscp_connection *next;
            Websock *sock;
        } ws;
        struct {
            struct espconn conn;
            esp_tcp tcp;
        } tcp;
    } d;
    char rxBuffer[SSCP_RX_BUFFER_MAX];
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
