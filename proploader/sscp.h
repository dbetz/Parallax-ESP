#ifndef __SSCP_H__
#define __SSCP_H__

#include "httpd.h"
#include "cgiwebsocket.h"

int cgiSSCPHandleRequest(HttpdConnData *connData);
void sscp_init(void);
void sscp_process(char *buf, short len);
void sscp_filter(char *buf, short len, void (*outOfBand)(void *data, char *buf, short len), void *data);
void sscp_websocketConnect(Websock *ws);

#endif

