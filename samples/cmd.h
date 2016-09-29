#ifndef __CMD_H__
#define __CMD_H__

#include <stdarg.h>
#include "simpletools.h"
#include "fdserial.h"

extern fdserial *debug;

#define dbg(args...) dprint(debug, args)

#define CMD_PREFIX  "\xFE"
#define CMD_START   0xFE

enum {
    CMD_TKN_START           = 0xFE,
    CMD_TKN_INT8            = 0xFD,
    CMD_TKN_UINT8           = 0xFC,
    CMD_TKN_INT16           = 0xFB,
    CMD_TKN_UINT16          = 0xFA,
    CMD_TKN_INT32           = 0xF9,
    CMD_TKN_UINT32          = 0xF8,
    
    CMD_TKN_HTTP            = 0xF7,
    CMD_TKN_WS              = 0xF6,
    CMD_TKN_TCP             = 0xF5,
    CMD_TKN_STA             = 0xF4,
    CMD_TKN_AP              = 0xF3,
    CMD_TKN_STA_AP          = 0xF2,
    
    // gap for more tokens
    
    CMD_TKN_JOIN            = 0xEF,
    CMD_TKN_CHECK           = 0xEE,
    CMD_TKN_SET             = 0xED,
    CMD_TKN_POLL            = 0xEC,
    CMD_TKN_PATH            = 0xEB,
    CMD_TKN_SEND            = 0xEA,
    CMD_TKN_RECV            = 0xE9,
    CMD_TKN_CLOSE           = 0xE8,
    CMD_TKN_LISTEN          = 0xE7,
    CMD_TKN_ARG             = 0xE6,
    CMD_TKN_REPLY           = 0xE5,
    CMD_TKN_CONNECT         = 0xE4,
    
    CMD_MIN_TOKEN           = 0x80
};

#define WIFI_QUEUE_MAX  1024

typedef enum {
    WIFI_STATE_START,
    WIFI_STATE_DATA
} wifi_state;

typedef struct wifi wifi;
typedef struct wifi_listener wifi_listener;
typedef struct wifi_request wifi_request;

struct wifi {
    fdserial *port;
    wifi_state messageState;
    char messageBuffer[WIFI_QUEUE_MAX];
    int messageHead;
    int messageTail;
    int messageStart;
    wifi_listener *listeners;
};

struct wifi_listener {
    wifi *dev;
    int handle;
    int (*doRequest)(wifi *wifi, wifi_request *request);
    wifi_request *requests;
};

struct wifi_request {
    wifi_listener *listener;
    int handle;
    void (*doDisconnect)(wifi_request *request);
    int (*doSent)(wifi_request *request, int count);
    int (*doError)(wifi_request *request, int err);
};

wifi *wifi_open(int wifi_rx, int wifi_tx);
int wifi_init(wifi *dev, int wifi_rx, int wifi_tx);
int wifi_getInteger(wifi *dev, const char *name, int *pValue);
int wifi_setInteger(wifi *dev, const char *name, int value);
int wifi_listen(wifi *dev, char *protocol, const char *path, int *pHandle);
int wifi_path(wifi *dev, int handle, char *path, int maxSize);
int wifi_arg(wifi *dev, int handle, const char *name, char *buf, int maxSize);
int wifi_reply(wifi *dev, int chan, int code, const char *payload);
int wifi_checkForEvent(wifi *dev, char *pType, int *pHandle, int *pListener);

#endif
