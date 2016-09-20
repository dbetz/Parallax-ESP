#ifndef __CMD_H__
#define __CMD_H__

#include <stdint.h>
#include "serial.h"

#define CMD_START_BYTE      0xFE
#define CMD_END_BYTE        '\r'

#define CMD_START           "\xFE"
#define CMD_INT8            "\xFD"
#define CMD_UINT8           "\xFC"
#define CMD_INT16           "\xFB"
#define CMD_UINT16          "\xFA"
#define CMD_INT32           "\xF9"
#define CMD_UINT32          "\xF8"
    
#define CMD_HTTP            "\xF7"
#define CMD_WS              "\xF6"
#define CMD_TCP             "\xF5"
#define CMD_STA             "\xF4"
#define CMD_AP              "\xF3"
#define CMD_STA_AP          "\xF2"
    
#define CMD_JOIN            "\xEF"
#define CMD_CHECK           "\xEE"
#define CMD_SET             "\xED"
#define CMD_POLL            "\xEC"
#define CMD_PATH            "\xEB"
#define CMD_SEND            "\xEA"
#define CMD_RECV            "\xE9"
#define CMD_CLOSE           "\xE8"
#define CMD_LISTEN          "\xE7"
#define CMD_ARG             "\xE6"
#define CMD_REPLY           "\xE5"
#define CMD_CONNECT         "\xE4"

#define CMD_END             "\r"

#define CMD_MAX_RESPONSE    1024
#define CMD_MAX_ARGS        16

#define WIFI_BUFFER_MAX     64
#define WIFI_QUEUE_MAX      1024

typedef enum {
    WIFI_STATE_START,
    WIFI_STATE_DATA
} wifi_state;

typedef struct wifi wifi;

struct wifi {
    SERIAL *port;
    uint8_t inputBuffer[WIFI_BUFFER_MAX];
    int inputNext;
    int inputCount;
    wifi_state messageState;
    char messageBuffer[WIFI_QUEUE_MAX];
    int messageHead;
    int messageTail;
    int messageStart;
};

int sscpOpen(wifi *dev, const char *deviceName);
int sscpClose(wifi *dev);
int sscpRequest(wifi *dev, const char *fmt, ...);
int sscpRequestPayload(wifi *dev, const char *buf, int len);
int sscpCollectPayload(wifi *dev, char *buf, int count);
int sscpGetResponse(wifi *dev, char *buf, int maxSize);
int sscpCheckForEvent(wifi *dev, char *buf, int maxSize);
int sscpParseResponse(char *buf, char *argv[], int maxArgs);

#endif
