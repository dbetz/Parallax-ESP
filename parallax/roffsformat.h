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
#ifndef ROFSFORMAT_H
#define ROFSFORMAT_H

/*
Stupid cpio-like tool to make read-only 'filesystems' that live on the flash SPI chip of the module.
Can (will) use lzf compression (when I come around to it) to make shit quicker. Aligns names, files,
headers on 4-byte boundaries so the SPI abstraction hardware in the ESP8266 doesn't crap on itself 
when trying to do a <4byte or unaligned read.
*/

/*
The idea 'borrows' from cpio: it's basically a concatenation of {header, filename, file} data.
Header, filename and file data is 32-bit aligned. The last file is indicated by data-less header
with the FLAG_LASTFILE flag set.
*/


#define FLAG_LASTFILE   (1 << 0)
#define FLAG_GZIP       (1 << 1)
#define FLAG_ACTIVE     (1 << 2)
#define FLAG_PENDING    (1 << 3)
#define COMPRESS_NONE   0
#define ROFS_MAGIC      ('R' | ('O' << 8) | ('f' << 16) | ('s' << 24))

typedef struct {
	int32_t magic;
	int8_t flags;
	int8_t compression;
	int16_t nameLen;
	int32_t fileLenComp;
	int32_t fileLenDecomp;
} __attribute__((packed)) RoFsHeader;

#endif
