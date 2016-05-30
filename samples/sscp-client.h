#ifndef __SSCP_CLIENT_H__
#define __SSCP_CLIENT_H__

#include "fdserial.h"

#define SSCP_PREFIX "\xFE"
#define SSCP_START  0xFE

void request(char *fmt, ...);
void requestPayload(char *buf, int len);
int waitFor(char *target);
void collectUntil(int term, char *buf, int size);
void collectPayload(char *buf, int bufSize, int count);
void skipUntil(int term);

int cgiPropSetting(HttpdConnData *connData);
int cgiPropSaveSettings(HttpdConnData *connData);
int cgiPropRestoreSettings(HttpdConnData *connData);
int cgiPropRestoreDefaultSettings(HttpdConnData *connData);

#endif
