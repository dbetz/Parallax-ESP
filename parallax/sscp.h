#ifndef __SSCP_H__
#define __SSCP_H__

#include "esp8266.h"
#include "httpd.h"
#include "cgiwebsocket.h"

#define SSCP_LISTENER_MAX   4
#define SSCP_PATH_MAX       32

#define SSCP_CONNECTION_MAX 4
#define SSCP_RX_BUFFER_MAX  1024
#define SSCP_TX_BUFFER_MAX  1024

#define SSCP_HANDLE_MAX     (SSCP_LISTENER_MAX + SSCP_CONNECTION_MAX)

enum {
    SSCP_TKN_START              = 0xFE,
    
    SSCP_TKN_INT8               = 0xFD,
    SSCP_TKN_UINT8              = 0xFC,
    SSCP_TKN_INT16              = 0xFB,
    SSCP_TKN_UINT16             = 0xFA,
    SSCP_TKN_INT32              = 0xF9,
    SSCP_TKN_UINT32             = 0xF8,
    
    SSCP_TKN_HTTP               = 0xF7,
    SSCP_TKN_WS                 = 0xF6,
    SSCP_TKN_TCP                = 0xF5,
    SSCP_TKN_STA                = 0xF4,
    SSCP_TKN_AP                 = 0xF3,
    SSCP_TKN_STA_AP             = 0xF2,
    
    // gap for more tokens
    
    SSCP_TKN_JOIN               = 0xEF,
    SSCP_TKN_CHECK              = 0xEE,
    SSCP_TKN_SET                = 0xED,
    SSCP_TKN_POLL               = 0xEC,
    SSCP_TKN_PATH               = 0xEB,
    SSCP_TKN_SEND               = 0xEA,
    SSCP_TKN_RECV               = 0xE9,
    SSCP_TKN_CLOSE              = 0xE8,
    SSCP_TKN_LISTEN             = 0xE7,
    SSCP_TKN_ARG                = 0xE6,
    SSCP_TKN_REPLY              = 0xE5,
    SSCP_TKN_CONNECT            = 0xE4,
    
    SSCP_MIN_TOKEN              = 0x80
};

typedef struct sscp_hdr sscp_hdr;
typedef struct sscp_listener sscp_listener;
typedef struct sscp_connection sscp_connection;

enum {
    SSCP_ERROR_INVALID_REQUEST      = 1,
    SSCP_ERROR_INVALID_ARGUMENT     = 2,
    SSCP_ERROR_WRONG_ARGUMENT_COUNT = 3,
    SSCP_ERROR_NO_FREE_LISTENER     = 4,
    SSCP_ERROR_NO_FREE_CONNECTION   = 5,
    SSCP_ERROR_LOOKUP_FAILED        = 6,
    SSCP_ERROR_CONNECT_FAILED       = 7,
    SSCP_ERROR_SEND_FAILED          = 8,
    SSCP_ERROR_INVALID_STATE        = 9,
    SSCP_ERROR_INVALID_SIZE         = 10,
    SSCP_ERROR_DISCONNECTED         = 11,
    SSCP_ERROR_UNIMPLEMENTED        = 12,
    SSCP_ERROR_BUSY                 = 13,
    SSCP_ERROR_INTERNAL_ERROR       = 14,
    SSCP_ERROR_INVALID_METHOD       = 15
};

enum {
    TYPE_UNUSED = 0,
    TYPE_HTTP_LISTENER,
    TYPE_WEBSOCKET_LISTENER,
    TYPE_TCP_LISTENER,
    TYPE_HTTP_CONNECTION,
    TYPE_WEBSOCKET_CONNECTION,
    TYPE_TCP_CONNECTION
};

typedef struct {
    int (*checkForEvents)(sscp_hdr *hdr);
    void (*path)(sscp_hdr *hdr); 
    void (*send)(sscp_hdr *hdr, int size);
    void (*recv)(sscp_hdr *hdr, int size); 
    void (*close)(sscp_hdr *hdr);
} sscp_dispatch;

struct sscp_hdr {
    int type;
    int handle;
    sscp_dispatch *dispatch;
};

struct sscp_listener {
    sscp_hdr hdr;
    char path[SSCP_PATH_MAX];
};

enum {
    // events that are deliverd to the MCU
    CONNECTION_INIT         = 0x00000001,   // set when a new request has been received ('G', 'P', 'T', 'W')
    CONNECTION_TERM         = 0x00000002,   // set when the remote end has closed a connection ('X')
    CONNECTION_FAIL         = 0x00000004,   // set when the connection has failed ('E')
    CONNECTION_TXFULL       = 0x00000004,   // set when outgoing data buffer is full ('D')
    CONNECTION_TXDONE       = 0x00000008,   // set when an outgoing transfer is complete ('S')

    // internal state bits
    CONNECTION_RXFULL       = 0x00010000,   // set when incoming data is available
    CONNECTION_TXFREE       = 0x00020000    // set when the connection should be freed after TXDONE is delivered
};

enum {
    TCP_STATE_IDLE = 0,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED
};

struct sscp_connection {
    sscp_hdr hdr;
    int flags;
    int error;
    int listenerHandle;
    union {
        struct {
            HttpdConnData *conn;
            int code;
            int count;
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
    int rxIndex;
    char txBuffer[SSCP_TX_BUFFER_MAX];
    int txCount;
    int txIndex;
};

extern sscp_listener sscp_listeners[];
extern sscp_connection sscp_connections[];

void sscp_init(void);
void sscp_reset(void);
void sscp_capturePayload(char *buf, int length, void (*cb)(void *data, int count), void *data);
void sscp_filter(char *buf, short len, void (*outOfBand)(void *data, char *buf, short len), void *data);

sscp_hdr *sscp_get_handle(int i);
sscp_listener *sscp_allocate_listener(int type, char *path, sscp_dispatch *dispatch);
sscp_listener *sscp_find_listener(const char *path, int type);
void sscp_close_listener(sscp_listener *listener);
sscp_connection *sscp_get_connection(int i);
sscp_connection *sscp_allocate_connection(int type, sscp_dispatch *dispatch);
void sscp_close_connection(sscp_connection *connection);
void sscp_sendResponse(char *fmt, ...);
void sscp_sendEvent(char *fmt, ...);
void sscp_send(int prefix, char *fmt, ...);
void sscp_sendPayload(char *buf, int cnt);
void sscp_log(char *fmt, ...);

// from sscp-cmds.c
void cmds_do_nothing(int argc, char *argv[]);
void cmds_do_listen(int argc, char *argv[]);
void cmds_do_join(int argc, char *argv[]);
void cmds_do_poll(int argc, char *argv[]);
void cmds_do_path(int argc, char *argv[]);
void cmds_do_send(int argc, char *argv[]);
void cmds_do_recv(int argc, char *argv[]);
void cmds_do_close(int argc, char *argv[]);
int cgiPropEnableSerialProtocol(HttpdConnData *connData);
int cgiPropModuleInfo(HttpdConnData *connData);

// from sscp-settings.c
void cmds_do_get(int argc, char *argv[]);
void cmds_do_set(int argc, char *argv[]);
int cgiPropSetting(HttpdConnData *connData);
int cgiPropSaveSettings(HttpdConnData *connData);
int cgiPropRestoreSettings(HttpdConnData *connData);
int cgiPropRestoreDefaultSettings(HttpdConnData *connData);
int tplSettings(HttpdConnData *connData, char *token, void **arg);

// from sscp-http.c
void http_do_listen(int argc, char *argv[]);
void http_do_arg(int argc, char *argv[]);
void http_do_body(int argc, char *argv[]);
void http_do_reply(int argc, char *argv[]);
int cgiSSCPHandleRequest(HttpdConnData *connData);
void http_disconnect(sscp_connection *connection);

// from sscp-ws.c
void sscp_websocketConnect(Websock *ws);

// from sscp-tcp.c
void tcp_do_connect(int argc, char *argv[]);

#endif
