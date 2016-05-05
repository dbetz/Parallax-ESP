#include <string.h>
#include <esp8266.h>
#include "proploader.h"
#include "uart.h"

// Propeller Download Stream Translator array.  Index into this array using the "Binary Value" (usually 5 bits) to translate,
// the incoming bit size (again, usually 5), and the desired data element to retrieve (encoding = translation, bitCount = bit count
// actually translated.

#if 0
// first index is the next 1-5 bits from the incoming bit stream
// second index is the number of bits in the first value
// the result is a structure containing the byte to output to encode some or all of the input bits
static struct {
    uint8_t encoding;   // encoded byte to output
    uint8_t bitCount;   // number of bits encoded by the output byte
} PDSTx[32][5] =

//  ***  1-BIT  ***        ***  2-BIT  ***        ***  3-BIT  ***        ***  4-BIT  ***        ***  5-BIT  ***
{ { /*%00000*/ {0xFE, 1},  /*%00000*/ {0xF2, 2},  /*%00000*/ {0x92, 3},  /*%00000*/ {0x92, 3},  /*%00000*/ {0x92, 3} },
  { /*%00001*/ {0xFF, 1},  /*%00001*/ {0xF9, 2},  /*%00001*/ {0xC9, 3},  /*%00001*/ {0xC9, 3},  /*%00001*/ {0xC9, 3} },
  {            {0,    0},  /*%00010*/ {0xFA, 2},  /*%00010*/ {0xCA, 3},  /*%00010*/ {0xCA, 3},  /*%00010*/ {0xCA, 3} },
  {            {0,    0},  /*%00011*/ {0xFD, 2},  /*%00011*/ {0xE5, 3},  /*%00011*/ {0x25, 4},  /*%00011*/ {0x25, 4} },
  {            {0,    0},             {0,    0},  /*%00100*/ {0xD2, 3},  /*%00100*/ {0xD2, 3},  /*%00100*/ {0xD2, 3} },
  {            {0,    0},             {0,    0},  /*%00101*/ {0xE9, 3},  /*%00101*/ {0x29, 4},  /*%00101*/ {0x29, 4} },
  {            {0,    0},             {0,    0},  /*%00110*/ {0xEA, 3},  /*%00110*/ {0x2A, 4},  /*%00110*/ {0x2A, 4} },
  {            {0,    0},             {0,    0},  /*%00111*/ {0xFA, 3},  /*%00111*/ {0x95, 4},  /*%00111*/ {0x95, 4} },
  {            {0,    0},             {0,    0},             {0,    0},  /*%01000*/ {0x92, 3},  /*%01000*/ {0x92, 3} },
  {            {0,    0},             {0,    0},             {0,    0},  /*%01001*/ {0x49, 4},  /*%01001*/ {0x49, 4} },
  {            {0,    0},             {0,    0},             {0,    0},  /*%01010*/ {0x4A, 4},  /*%01010*/ {0x4A, 4} },
  {            {0,    0},             {0,    0},             {0,    0},  /*%01011*/ {0xA5, 4},  /*%01011*/ {0xA5, 4} },
  {            {0,    0},             {0,    0},             {0,    0},  /*%01100*/ {0x52, 4},  /*%01100*/ {0x52, 4} },
  {            {0,    0},             {0,    0},             {0,    0},  /*%01101*/ {0xA9, 4},  /*%01101*/ {0xA9, 4} },
  {            {0,    0},             {0,    0},             {0,    0},  /*%01110*/ {0xAA, 4},  /*%01110*/ {0xAA, 4} },
  {            {0,    0},             {0,    0},             {0,    0},  /*%01111*/ {0xD5, 4},  /*%01111*/ {0xD5, 4} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%10000*/ {0x92, 3} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%10001*/ {0xC9, 3} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%10010*/ {0xCA, 3} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%10011*/ {0x25, 4} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%10100*/ {0xD2, 3} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%10101*/ {0x29, 4} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%10110*/ {0x2A, 4} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%10111*/ {0x95, 4} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%11000*/ {0x92, 3} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%11001*/ {0x49, 4} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%11010*/ {0x4A, 4} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%11011*/ {0xA5, 4} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%11100*/ {0x52, 4} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%11101*/ {0xA9, 4} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%11110*/ {0xAA, 4} },
  {            {0,    0},             {0,    0},             {0,    0},             {0,    0},  /*%11111*/ {0x55, 5} }
 };
#endif

// After reset, the Propeller's exact clock rate is not known by either the host or the Propeller itself, so communication
// with the Propeller takes place based on a host-transmitted timing template that the Propeller uses to read the stream
// and generate the responses.  The host first transmits the 2-bit timing template, then transmits a 250-bit Tx handshake,
// followed by 250 timing templates (one for each Rx handshake bit expected) which the Propeller uses to properly transmit
// the Rx handshake sequence.  Finally, the host transmits another eight timing templates (one for each bit of the
// Propeller's version number expected) which the Propeller uses to properly transmit it's 8-bit hardware/firmware version
// number.
//
// After the Tx Handshake and Rx Handshake are properly exchanged, the host and Propeller are considered "connected," at
// which point the host can send a download command followed by image size and image data, or simply end the communication.
//
// PROPELLER HANDSHAKE SEQUENCE: The handshake (both Tx and Rx) are based on a Linear Feedback Shift Register (LFSR) tap
// sequence that repeats only after 255 iterations.  The generating LFSR can be created in Pascal code as the following function
// (assuming FLFSR is pre-defined Byte variable that is set to ord('P') prior to the first call of IterateLFSR).  This is
// the exact function that was used in previous versions of the Propeller Tool and Propellent software.
//
//      function IterateLFSR: Byte;
//      begin //Iterate LFSR, return previous bit 0
//      Result := FLFSR and 0x01;
//      FLFSR := FLFSR shl 1 and 0xFE or (FLFSR shr 7 xor FLFSR shr 5 xor FLFSR shr 4 xor FLFSR shr 1) and 1;
//      end;
//
// The handshake bit stream consists of the lowest bit value of each 8-bit result of the LFSR described above.  This LFSR
// has a domain of 255 combinations, but the host only transmits the first 250 bits of the pattern, afterwards, the Propeller
// generates and transmits the next 250-bits based on continuing with the same LFSR sequence.  In this way, the host-
// transmitted (host-generated) stream ends 5 bits before the LFSR starts repeating the initial sequence, and the host-
// received (Propeller generated) stream that follows begins with those remaining 5 bits and ends with the leading 245 bits
// of the host-transmitted stream.
//
// For speed and compression reasons, this handshake stream has been encoded as tightly as possible into the pattern
// described below.
//
// The TxHandshake array consists of 209 bytes that are encoded to represent the required '1' and '0' timing template bits,
// 250 bits representing the lowest bit values of 250 iterations of the Propeller LFSR (seeded with ASCII 'P'), 250 more
// timing template bits to receive the Propeller's handshake response, and more to receive the version.
static const uint8_t txHandshake[] = {
    // First timing template ('1' and '0') plus first two bits of handshake ('0' and '1').
    0x49,
    // Remaining 248 bits of handshake...
    0xAA,0x52,0xA5,0xAA,0x25,0xAA,0xD2,0xCA,0x52,0x25,0xD2,0xD2,0xD2,0xAA,0x49,0x92,
    0xC9,0x2A,0xA5,0x25,0x4A,0x49,0x49,0x2A,0x25,0x49,0xA5,0x4A,0xAA,0x2A,0xA9,0xCA,
    0xAA,0x55,0x52,0xAA,0xA9,0x29,0x92,0x92,0x29,0x25,0x2A,0xAA,0x92,0x92,0x55,0xCA,
    0x4A,0xCA,0xCA,0x92,0xCA,0x92,0x95,0x55,0xA9,0x92,0x2A,0xD2,0x52,0x92,0x52,0xCA,
    0xD2,0xCA,0x2A,0xFF,
    // 250 timing templates ('1' and '0') to receive 250-bit handshake from Propeller.
    // This is encoded as two pairs per byte; 125 bytes.
    0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,
    0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,
    0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,
    0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,
    0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,
    0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,
    0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,
    0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,0x29,
    // 8 timing templates ('1' and '0') to receive 8-bit Propeller version; two pairs per byte; 4 bytes.
    0x29,0x29,0x29,0x29};

// Shutdown command (0); 11 bytes.
static const uint8_t shutdownCmd[] = {
    0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0xf2};

// Load RAM and Run command (1); 11 bytes.
static const uint8_t loadRunCmd[] = {
    0xc9, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0xf2};

// Load RAM, Program EEPROM, and Shutdown command (2); 11 bytes.
static const uint8_t programShutdownCmd[] = {
    0xca, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0xf2};

// Load RAM, Program EEPROM, and Run command (3); 11 bytes.
static const uint8_t programRunCmd[] = {
    0x25, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0xfe};

// The RxHandshake array consists of 125 bytes encoded to represent the expected 250-bit (125-byte @ 2 bits/byte) response
// of continuing-LFSR stream bits from the Propeller, prompted by the timing templates following the TxHandshake stream.
static const uint8_t rxHandshake[] = {
    0xEE,0xCE,0xCE,0xCF,0xEF,0xCF,0xEE,0xEF,0xCF,0xCF,0xEF,0xEF,0xCF,0xCE,0xEF,0xCF,
    0xEE,0xEE,0xCE,0xEE,0xEF,0xCF,0xCE,0xEE,0xCE,0xCF,0xEE,0xEE,0xEF,0xCF,0xEE,0xCE,
    0xEE,0xCE,0xEE,0xCF,0xEF,0xEE,0xEF,0xCE,0xEE,0xEE,0xCF,0xEE,0xCF,0xEE,0xEE,0xCF,
    0xEF,0xCE,0xCF,0xEE,0xEF,0xEE,0xEE,0xEE,0xEE,0xEF,0xEE,0xCF,0xCF,0xEF,0xEE,0xCE,
    0xEF,0xEF,0xEF,0xEF,0xCE,0xEF,0xEE,0xEF,0xCF,0xEF,0xCF,0xCF,0xCE,0xCE,0xCE,0xCF,
    0xCF,0xEF,0xCE,0xEE,0xCF,0xEE,0xEF,0xCE,0xCE,0xCE,0xEF,0xEF,0xCF,0xCF,0xEE,0xEE,
    0xEE,0xCE,0xCF,0xCE,0xCE,0xCF,0xCE,0xEE,0xEF,0xEE,0xEF,0xEF,0xCF,0xEF,0xCE,0xCE,
    0xEF,0xCE,0xEE,0xCE,0xEF,0xCE,0xCE,0xEE,0xCF,0xCF,0xCE,0xCF,0xCF};

static int startLoad(PropellerConnection *connection, LoadType loadType, int imageSize);
static int encodeFile(PropellerConnection *connection, int *pFinished);
static int encodeBuffer(PropellerConnection *connection, const uint8_t *buffer, int size);
static void finishLoad(PropellerConnection *connection);
static void txLong(uint32_t x);

int ICACHE_FLASH_ATTR ploadInitiateHandshake(PropellerConnection *connection)
{
    uart0_tx_buffer((char *)txHandshake, (uint16_t)sizeof(txHandshake));
    connection->bytesRemaining = sizeof(rxHandshake) + 4;
    connection->bytesReceived = 0;
    return 0;
}

int ICACHE_FLASH_ATTR ploadVerifyHandshakeResponse(PropellerConnection *connection, int *pVersion)
{
    uint8_t *buf = connection->buffer;
    int version, i;

    /* verify the rx handshake */
    if (memcmp(buf, rxHandshake, sizeof(rxHandshake)) != 0)
        return -1;

    /* verify the hardware version */
    version = 0;
    for (i = sizeof(rxHandshake); i < sizeof(rxHandshake) + 4; ++i)
        version = ((version >> 2) & 0x3F) | ((buf[i] & 0x01) << 6) | ((buf[i] & 0x20) << 2);
    *pVersion = version;

    return 0;
}

int ICACHE_FLASH_ATTR ploadLoadImage(PropellerConnection *connection, LoadType loadType, int *pFinished)
{
    if (startLoad(connection, loadType, connection->imageSize) != 0)
        return -1;
    return ploadLoadImageContinue(connection, loadType, pFinished);
}

int ICACHE_FLASH_ATTR ploadLoadImageContinue(PropellerConnection *connection, LoadType loadType, int *pFinished)
{
    if (connection->image) {
        if (encodeBuffer(connection, connection->image, connection->imageSize) != 0)
            return -1;
        connection->image = NULL;
        *pFinished = 1;
    }
    else if (connection->file) {
        if (encodeFile(connection, pFinished) != 0)
            return -1;
        if (*pFinished) {
            roffs_close(connection->file);
            connection->file = NULL;
        }
    }
    else
        return -1;

    if (*pFinished)
        finishLoad(connection);

    return 0;
}

static int ICACHE_FLASH_ATTR startLoad(PropellerConnection *connection, LoadType loadType, int imageSize)
{
    switch (loadType) {
    case ltShutdown:
        uart0_tx_buffer((char *)shutdownCmd, (uint16_t)sizeof(shutdownCmd));
        break;
    case ltDownloadAndRun:
        uart0_tx_buffer((char *)loadRunCmd, (uint16_t)sizeof(loadRunCmd));
        break;
    case ltDownloadAndProgram:
        uart0_tx_buffer((char *)programShutdownCmd, (uint16_t)sizeof(loadRunCmd));
        break;
    case ltDownloadAndProgramAndRun:
        uart0_tx_buffer((char *)programRunCmd, (uint16_t)sizeof(loadRunCmd));
        break;
    default:
        return -1;
    }
    
    if (loadType != ltShutdown) {
        txLong(imageSize / 4);
        connection->encodedSize = 0;
    }

    return 0;
}

static int ICACHE_FLASH_ATTR encodeFile(PropellerConnection *connection, int *pFinished)
{
    uint8_t buffer[LOAD_SEGMENT_MAX_SIZE];
    int readSize;

    if ((readSize = connection->imageSize) <= 0) {
        *pFinished = 1;
        return 0;
    }

    if (readSize > sizeof(buffer))
        readSize = sizeof(buffer);

    if (roffs_read(connection->file, (char *)buffer, readSize) != readSize)
        return -1;

    if (encodeBuffer(connection, buffer, readSize) != 0)
        return -1;

    if ((connection->imageSize -= readSize) == 0)
        *pFinished = 1;
    else
        *pFinished = 0;

    return 0;
}

static int ICACHE_FLASH_ATTR encodeBuffer(PropellerConnection *connection, const uint8_t *buffer, int size)
{
#if 0
    static uint8_t masks[] = { 0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f };
    int bitCount = size * 8;
    int nextBit = 0;

    /* encode all bits in the input buffer */
    while (nextBit < bitCount) {
        int bits, bitsIn;

        /* encode 5 bits or whatever remains in inBytes, whichever is smaller */
        bitsIn = bitCount - nextBit;
        if (bitsIn > 5)
            bitsIn = 5;

        /* extract the next 'bitsIn' bits from the input buffer */
        bits = ((buffer[nextBit / 8] >> (nextBit % 8))
              | (buffer[nextBit / 8 + 1] << (8 - (nextBit % 8)))) & masks[bitsIn];

        /* transmit the encoded value */
        uart_tx_one_char(UART0, PDSTx[bits][bitsIn - 1].encoding);
        ++connection->encodedSize;

        /* advance to the next group of bits */
        nextBit += PDSTx[bits][bitsIn - 1].bitCount;
    }
#else
    uint32_t *p = (uint32_t *)buffer;
    while ((size -= sizeof(uint32_t)) >= 0)
        txLong(*p++);
#endif

    return 0;
}

static void ICACHE_FLASH_ATTR finishLoad(PropellerConnection *connection)
{
    int tmp = (connection->encodedSize * 10 * 1000) / connection->baudRate;
    connection->retriesRemaining = (tmp + 250) / CALIBRATE_DELAY;
    connection->retryDelay = CALIBRATE_DELAY;
}

static void ICACHE_FLASH_ATTR txLong(uint32_t x)
{
    int i;
    for (i = 0; i < 11; ++i) {
        uart_tx_one_char(UART0, 0x92 
                              | (i == 10 ? 0x60 : 0x00)
                              |  (x & 1)
                              | ((x & 2) << 2)
                              | ((x & 4) << 4));
        x >>= 3;
    }
}


