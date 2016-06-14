#ifndef __SSCP_CLIENT_H__
#define __SSCP_CLIENT_H__

#include "fdserial.h"

#define SSCP_PREFIX "\xFE"
#define SSCP_START  0xFE

enum {
    SSCP_TKN_START              = 0xFE,
    SSCP_TKN_INT8               = 0xFD,
    SSCP_TKN_UINT8              = 0xFC,
    SSCP_TKN_INT16              = 0xFB,
    SSCP_TKN_UINT16             = 0xFA,
    SSCP_TKN_INT32              = 0xF9,
    SSCP_TKN_UINT32             = 0xF8,
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

void request(char *fmt, ...);
void nrequest(int token, char *fmt, ...);
void requestPayload(char *buf, int len);
int reply(int chan, int code, char *payload);
int waitFor(char *fmt, ...);
void collectUntil(int term, char *buf, int size);
void collectPayload(char *buf, int bufSize, int count);
void skipUntil(int term);

#endif
