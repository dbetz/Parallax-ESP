#ifndef CGIPROP_H
#define CGIPROP_H

#include <httpd.h>

int cgiPropInit();
int cgiPropVersion(HttpdConnData *connData);
int cgiPropBaudRate(HttpdConnData *connData);
int cgiPropLoaderBaudRate(HttpdConnData *connData);
int cgiPropSetting(HttpdConnData *connData);
int cgiPropEnableSerialProtocol(HttpdConnData *connData);
int cgiPropSaveSettings(HttpdConnData *connData);
int cgiPropRestoreSettings(HttpdConnData *connData);
int cgiPropRestoreDefaultSettings(HttpdConnData *connData);
int cgiPropLoad(HttpdConnData *connData);
int cgiPropLoadFile(HttpdConnData *connData);
int cgiPropReset(HttpdConnData *connData);

void httpdSendResponse(HttpdConnData *connData, int code, char *message, int len);

#endif

