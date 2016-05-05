#ifndef PROPIMAGE_H
#define PROPIMAGE_H

//#include <stdint.h>
#include "os_type.h"

/* target checksum for a binary file */
#define SPIN_TARGET_CHECKSUM    0x14

/* spin object file header */
typedef struct {
    uint32_t clkfreq;
    uint8_t clkmode;
    uint8_t chksum;
    uint16_t pbase;
    uint16_t vbase;
    uint16_t dbase;
    uint16_t pcurr;
    uint16_t dcurr;
} SpinHdr;

typedef struct {
    uint8_t *imageData;
    int imageSize;
} PropellerImage;

void pimageSetImage(PropellerImage *image, uint8_t *imageData, int imageSize);
uint32_t pimageClkFreq(PropellerImage *image);
void pimageSetClkFreq(PropellerImage *image, uint32_t clkFreq);
uint8_t pimageClkMode(PropellerImage *image);
void pimageSetClkMode(PropellerImage *image, uint8_t clkMode);
uint8_t pimageUpdateChecksum(PropellerImage *image);
uint8_t pimageGetByte(PropellerImage *image, int offset);
void pimageSetByte(PropellerImage *image, int offset, uint8_t value);
uint16_t pimageGetWord(PropellerImage *image, int offset);
void pimageSetWord(PropellerImage *image, int offset, uint16_t value);
uint32_t pimageGetLong(PropellerImage *image, int offset);
void pimageSetLong(PropellerImage *image, int offset, uint32_t value);

#endif

