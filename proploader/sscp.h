#ifndef __SSCP_H__
#define __SSCP_H__

#include "esp8266.h"
#include "httpd.h"
#include "cgiwebsocket.h"

#define SSCP_LISTENER_MAX   2
#define SSCP_PATH_MAX       32

#define SSCP_CONNECTION_MAX 4
#define SSCP_RX_BUFFER_MAX  1024
#define SSCP_TX_BUFFER_MAX  1024

typedef struct sscp_listener sscp_listener;
typedef struct sscp_connection sscp_connection;

enum {
    SSCP_ERROR_INVALID_REQUEST      = -1,
    SSCP_ERROR_INVALID_ARGUMENT     = -2,
    SSCP_ERROR_WRONG_ARGUMENT_COUNT = -3,
    SSCP_ERROR_NO_FREE_LISTENER     = -4,
    SSCP_ERROR_NO_FREE_CONNECTION   = -5,
    SSCP_ERROR_LOOKUP_FAILED        = -6,
    SSCP_ERROR_CONNECT_FAILED       = -7,
    SSCP_ERROR_SEND_FAILED          = -8,
    SSCP_ERROR_INVALID_STATE        = -9,
    SSCP_ERROR_INVALID_SIZE         = -10,
    SSCP_ERROR_DISCONNECTED         = -11,
    SSCP_ERROR_UNIMPLEMENTED        = -12,
    SSCP_ERROR_BUSY                 = -13
};

enum {
    LISTENER_UNUSED = 0,
    LISTENER_HTTP,
    LISTENER_WEBSOCKET,
    LISTENER_TCP
};

struct sscp_listener {
    int type;
    char path[SSCP_PATH_MAX];
    sscp_connection *connections;
};

enum {
    CONNECTION_INIT         = 0x00000001,
    CONNECTION_RXFULL       = 0x00000002,
    CONNECTION_TXFULL       = 0x00000004
};

enum {
    TCP_STATE_IDLE = 0,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED
};

struct sscp_connection {
    int index;
    int flags;
    sscp_listener *listener;
    sscp_connection *next;
    union {
        struct {
            HttpdConnData *conn;
            int code;
        } http;
        struct {
            Websock *ws;
        } ws;
        struct {
            int state;
            struct espconn conn;
            esp_tcp tcp;
        } tcp;
    } d;
    char rxBuffer[SSCP_RX_BUFFER_MAX];
    int rxCount;
    char txBuffer[SSCP_TX_BUFFER_MAX];
    int txCount;
};

extern sscp_listener sscp_listeners[];
extern sscp_connection sscp_connections[];

void sscp_init(void);
void sscp_enable(int enable);
int sscp_isEnabled(void);
void sscp_capturePayload(char *buf, int length, void (*cb)(void *data), void *data);
void sscp_filter(char *buf, short len, void (*outOfBand)(void *data, char *buf, short len), void *data);
void sscp_websocketConnect(Websock *ws);

sscp_listener *sscp_get_listener(int i);
sscp_listener *sscp_find_listener(const char *path, int type);
void sscp_close_listener(sscp_listener *listener);
sscp_connection *sscp_get_connection(int i);
sscp_connection *sscp_allocate_connection(sscp_listener *listener);
void sscp_free_connection(sscp_connection *connection);
void sscp_remove_connection(sscp_connection *connection);
void sscp_sendResponse(char *fmt, ...);
void sscp_sendPayload(char *buf, int cnt);

// from sscp-cmds.c
void cmds_do_nothing(int argc, char *argv[]);
void cmds_do_join(int argc, char *argv[]);
void cmds_do_get(int argc, char *argv[]);
void cmds_do_set(int argc, char *argv[]);
void cmds_do_poll(int argc, char *argv[]);
void cmds_do_send(int argc, char *argv[]);
void cmds_do_recv(int argc, char *argv[]);
int cgiPropSetting(HttpdConnData *connData);
int cgiPropEnableSerialProtocol(HttpdConnData *connData);
int cgiPropSaveSettings(HttpdConnData *connData);
int cgiPropRestoreSettings(HttpdConnData *connData);
int cgiPropRestoreDefaultSettings(HttpdConnData *connData);

// from sscp-http.c
void http_do_listen(int argc, char *argv[]);
void http_do_arg(int argc, char *argv[]);
void http_do_postarg(int argc, char *argv[]);
void http_do_reply(int argc, char *argv[]);
int cgiSSCPHandleRequest(HttpdConnData *connData);

// from sscp-ws.c
void ws_do_wslisten(int argc, char *argv[]);
void ws_send_helper(sscp_connection *connection, int argc, char *argv[]);

// from sscp-tcp.c
void tcp_do_connect(int argc, char *argv[]);
void tcp_do_disconnect(int argc, char *argv[]);
void tcp_send_helper(sscp_connection *c, int argc, char *argv[]);

#endif
