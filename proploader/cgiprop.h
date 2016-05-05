#ifndef CGIPROP_H
#define CGIPROP_H

#include <httpd.h>

int cgiPropInit();
int cgiPropSetBaudRate(HttpdConnData *connData);
int cgiPropLoad(HttpdConnData *connData);
int cgiPropLoadFile(HttpdConnData *connData);
int cgiPropReset(HttpdConnData *connData);

#endif

