#ifndef __CMD_H__
#define __CMD_H__

#include "fdserial.h"

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

extern fdserial *wifi;
extern fdserial *debug;

void cmd_init(int wifi_rx, int wifi_tx, int debug_rx, int debug_tx);
void request(char *fmt, ...);
void nrequest(int token, char *fmt, ...);
void requestPayload(char *buf, int len);
int reply(int chan, int code, char *payload);
int waitFor(char *fmt, ...);
void collectUntil(int term, char *buf, int size);
void collectPayload(char *buf, int bufSize, int count);
void skipUntil(int term);

#endif
