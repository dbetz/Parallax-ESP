/*
    proploader.h - definitions for the Parallax Propeller binary image loader

	Copyright (c) 2016 Parallax Inc.
    See the file LICENSE.txt for licensing information.
*/

#ifndef PROPLOADER_H
#define PROPLOADER_H

#include <osapi.h>
#include "os_type.h"
#include "httpd.h"
#include "roffs.h"

//#define PROP_DBG

#ifdef PROP_DBG
#define DBG(format, ...) os_printf(format, ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

typedef enum {
    ltShutdown = 0,
    ltDownloadAndRun = (1 << 0),
    ltDownloadAndProgram = (1 << 1),
    ltDownloadAndProgramAndRun = ltDownloadAndRun | ltDownloadAndProgram
} LoadType;

typedef enum {
/* 0 */    stIdle,
/* 1 */    stReset,
/* 2 */    stTxHandshake,
/* 3 */    stRxHandshakeStart,
/* 4 */    stRxHandshake,
/* 5 */    stLoadContinue,
/* 6 */    stVerifyChecksum,
/* 7 */    stStartAck, 
           stMAX
} LoadState;

typedef enum {
    lsOK,
    lsAckResponse,
    lsFirstError,
    lsBusy = lsFirstError,
    lsFileNotFound,
    lsRXHandshakeTimeout,
    lsChecksumTimeout,
    lsStartAckTImeout,
    lsRXHandshakeFailed,
    lsWrongPropellerVersion,
    lsLoadImageFailed,
    lsChecksumError
} LoadStatus;

typedef struct PropellerConnection PropellerConnection;

struct PropellerConnection {
    HttpdConnData *connData;
    ETSTimer timer;
    int resetPin;
    int baudRate;
    int finalBaudRate;
    int responseSize;
    int responseTimeout;
    LoadType loadType;
    ROFFS_FILE *file;       // this is set for loading a file
    const uint8_t *image;   // this is set for loading an image in memory
    int imageSize;
    int encodedSize;
    LoadState state;
    int retriesRemaining;
    int retryDelay;
    uint8_t buffer[125 + 4]; // sizeof(rxHandshake) + 4
    int bytesReceived;
    int bytesRemaining;
    int version;
    void (*completionCB)(PropellerConnection *connection, LoadStatus status);
};

#define RESET_BUTTON_PIN                0
#define RESET_BUTTON_SAMPLE_INTERVAL    5
#define RESET_BUTTON_THRESHOLD          5
#define RESET_BUTTON_PRESS_DELTA        500
#define RESET_BUTTON_PRESS_COUNT        4

#define RESET_DELAY_1                   10
#define RESET_DELAY_2                   100
#define CALIBRATE_DELAY                 10

#define LOAD_SEGMENT_MAX_SIZE           1024
#define LOAD_SEGMENT_DELAY              50
#define RX_HANDSHAKE_TIMEOUT            2000
#define RX_CHECKSUM_TIMEOUT             250
#define EEPROM_PROGRAM_TIMEOUT          5000
#define EEPROM_VERIFY_TIMEOUT           2000

LoadStatus loadBuffer(const uint8_t *image, int imageSize);
LoadStatus loadFile(char *fileName);

int ploadInitiateHandshake(PropellerConnection *connection);
int ploadVerifyHandshakeResponse(PropellerConnection *connection);
int ploadLoadImage(PropellerConnection *connection, LoadType loadType, int *pFinished);
int ploadLoadImageContinue(PropellerConnection *connection, LoadType loadType, int *pFinished);

void httpdSendResponse(HttpdConnData *connData, int code, char *message, int len);

#endif

