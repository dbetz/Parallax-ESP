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
#ifndef __ROFFS_H__
#define __ROFFS_H__

#include <esp8266.h>

#define FLASH_FILESYSTEM_BASE   0x100000

/* must match definitions in roffsformat.h */
#define ROFFS_FLAG_GZIP (1<<1)

#include "roffsformat.h"
typedef struct ROFFS_FILE_STRUCT ROFFS_FILE;

int roffs_mount(uint32_t flashAddress);
int roffs_format(uint32_t flashAddress);
ROFFS_FILE *roffs_open(const char *fileName);
int roffs_file_size(ROFFS_FILE *file);
int roffs_file_flags(ROFFS_FILE *file);
int roffs_read(ROFFS_FILE *file, char *buf, int len);
int roffs_close(ROFFS_FILE *file);

ROFFS_FILE *roffs_create(const char *fileName);
int roffs_write(ROFFS_FILE *file, char *buf, int len);

#endif


