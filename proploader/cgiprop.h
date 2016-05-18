#ifndef CGIPROP_H
#define CGIPROP_H

#include <httpd.h>

int cgiPropInit();
int cgiPropBaudRate(HttpdConnData *connData);
int cgiPropEnableSerialProtocol(HttpdConnData *connData);
int cgiPropLoad(HttpdConnData *connData);
int cgiPropLoadFile(HttpdConnData *connData);
int cgiPropReset(HttpdConnData *connData);

#endif

