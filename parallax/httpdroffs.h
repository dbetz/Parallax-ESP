/*
	Copyright (c) 2016 Parallax Inc.
    See the file LICENSE.txt for licensing information.

Derived from:

Connector to let httpd use the espfs filesystem to serve the files in it.
*/

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain
 * this notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return.
 * ----------------------------------------------------------------------------
 * Modified and enhanced by Thorsten von Eicken in 2015
 * ----------------------------------------------------------------------------
 */
#ifndef HTTPDROFFS_H
#define HTTPDROFFS_H

#include <esp8266.h>
#include "httpd.h"

int cgiRoffsHook(HttpdConnData *connData);
int cgiRoffsFormat(HttpdConnData *connData);
int cgiRoffsWriteFile(HttpdConnData *connData);

#endif
