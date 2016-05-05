#ifndef __SSCP_H__
#define __SSCP_H__

#include "httpd.h"

int cgiSSCPHandleRequest(HttpdConnData *connData);
void sscp_init(void);
void sscp_process(char *buf, short len);
void sscp_filter(char *buf, short len, void (*outOfBand)(void *data, char *buf, short len), void *data);

#endif

