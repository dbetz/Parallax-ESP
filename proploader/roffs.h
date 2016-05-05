#ifndef __ROFFS_H__
#define __ROFFS_H__

#include <esp8266.h>

#define FLASH_FILESYSTEM_BASE   0x100000

/* must match definitions in roffsformat.h */
#define ROFFS_FLAG_GZIP (1<<1)

#ifdef SPIFFS
#include "spiffs.h"
typedef struct {
    spiffs_file fd;
} ROFFS_FILE;
#else
#include "roffsformat.h"
typedef struct ROFFS_FILE_STRUCT ROFFS_FILE;
#endif

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


