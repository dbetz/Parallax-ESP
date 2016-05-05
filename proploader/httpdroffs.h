#ifndef HTTPDROFFS_H
#define HTTPDROFFS_H

#include <esp8266.h>
#include "httpd.h"

int cgiRoffsHook(HttpdConnData *connData);
int cgiRoffsFormat(HttpdConnData *connData);
int cgiRoffsWriteFile(HttpdConnData *connData);

#endif
