/*
    propimage.c - support for the Parallax Propeller binary images

	Copyright (c) 2016 Parallax Inc.
    See the file LICENSE.txt for licensing information.
*/

#include "propimage.h"

#define OFFSET_OF(_s, _f) ((int)&((_s *)0)->_f)

void ICACHE_FLASH_ATTR pimageSetImage(PropellerImage *image, uint8_t *imageData, int imageSize)
{
    image->imageData = imageData;
    image->imageSize = imageSize;
}

uint32_t ICACHE_FLASH_ATTR pimageClkFreq(PropellerImage *image)
{
    return pimageGetLong(image, OFFSET_OF(SpinHdr, clkfreq));
}

void ICACHE_FLASH_ATTR pimageSetClkFreq(PropellerImage *image, uint32_t clkFreq)
{
    pimageSetLong(image, OFFSET_OF(SpinHdr, clkfreq), clkFreq);
}

uint8_t ICACHE_FLASH_ATTR pimageClkMode(PropellerImage *image)
{
    return pimageGetByte(image, OFFSET_OF(SpinHdr, clkmode));
}

void ICACHE_FLASH_ATTR pimageSetClkMode(PropellerImage *image, uint8_t clkMode)
{
    pimageSetByte(image, OFFSET_OF(SpinHdr, clkmode), clkMode);
}

uint8_t ICACHE_FLASH_ATTR pimageUpdateChecksum(PropellerImage *image)
{
    SpinHdr *spinHdr = (SpinHdr *)image->imageData;
    uint8_t *p = image->imageData;
    int chksum, cnt;
    spinHdr->chksum = chksum = 0;
    for (cnt = image->imageSize; --cnt >= 0; )
        chksum += *p++;
    spinHdr->chksum = SPIN_TARGET_CHECKSUM - chksum;
    return chksum & 0xff;
}

uint8_t ICACHE_FLASH_ATTR pimageGetByte(PropellerImage *image, int offset)
{
     uint8_t *buf = image->imageData + offset;
     return buf[0];
}

void ICACHE_FLASH_ATTR pimageSetByte(PropellerImage *image, int offset, uint8_t value)
{
     uint8_t *buf = image->imageData + offset;
     buf[0] = value;
}

uint16_t ICACHE_FLASH_ATTR pimageGetWord(PropellerImage *image, int offset)
{
     uint8_t *buf = image->imageData + offset;
     return (buf[1] << 8) | buf[0];
}

void ICACHE_FLASH_ATTR pimageSetWord(PropellerImage *image, int offset, uint16_t value)
{
     uint8_t *buf = image->imageData + offset;
     buf[1] = value >>  8;
     buf[0] = value;
}

uint32_t ICACHE_FLASH_ATTR pimageGetLong(PropellerImage *image, int offset)
{
     uint8_t *buf = image->imageData + offset;
     return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
}

void ICACHE_FLASH_ATTR pimageSetLong(PropellerImage *image, int offset, uint32_t value)
{
     uint8_t *buf = image->imageData + offset;
     buf[3] = value >> 24;
     buf[2] = value >> 16;
     buf[1] = value >>  8;
     buf[0] = value;
}

