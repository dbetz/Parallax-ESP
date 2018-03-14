/*
    cgiprop.h - definitions for HTTP requests related to the Parallax Propeller

	Copyright (c) 2016 Parallax Inc.
    See the file LICENSE.txt for licensing information.
*/

#ifndef CGIPROP_H
#define CGIPROP_H

#include <httpd.h>

int cgiPropInit();
int cgiPropVersion(HttpdConnData *connData);
int cgiPropLoad(HttpdConnData *connData);
int cgiPropLoadFile(HttpdConnData *connData);
int cgiPropReset(HttpdConnData *connData);

void httpdSendResponse(HttpdConnData *connData, int code, char *message, int len);

int IsAutoLoadEnabled(void);

#endif

