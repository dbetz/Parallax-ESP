#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "cmd.h"

#define dbg(args...) dprint(debug, args)

int debug;
int dprint(int port, const char *fmt, ...)
{
    va_list ap;
    int len;
    va_start(ap, fmt);
    len = vfprintf(stderr, fmt, ap);
    va_end(ap);
    return len;
}

int sscpOpen(wifi *dev, const char *deviceName)
{
    memset(dev, 0, sizeof(*dev));

    if (OpenSerial(deviceName, 115200, &dev->port) != 0)
        return -1;

    dev->messageState = WIFI_STATE_START;
    SerialSendBreak(dev->port);

    return 0;
}

int sscpClose(wifi *dev)
{
    if (dev->port) {
        CloseSerial(dev->port);
        dev->port = NULL;
    }
    return 0;
}

int sscpRequest(wifi *dev, const char *fmt, ...)
{
    char buf[100];
    int len;
    
    va_list ap;
    va_start(ap, fmt);
    len = vsnprintf(&buf[1], sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    
    buf[0] = CMD_START_BYTE;
    buf[len + 1] = CMD_END_BYTE;
    return SendSerialData(dev->port, buf, len + 2);
}

int sscpRequestPayload(wifi *dev, const char *buf, int len)
{
    return SendSerialData(dev->port, buf, len);
}

int sscpCollectPayload(wifi *dev, char *buf, int count)
{
    return ReceiveSerialData(dev->port, buf, count);
}

static int rxCheck(wifi *dev)
{
    if (dev->inputNext >= dev->inputCount) {
        dev->inputNext = 0;
        if ((dev->inputCount = ReceiveSerialData(dev->port, dev->inputBuffer, sizeof(dev->inputBuffer))) <= 0) {
            dev->inputCount = 0;
            return -1;
        }
    }
    return dev->inputBuffer[dev->inputNext++];
}

static int checkForMessage(wifi *dev, int type, char *buf, int maxSize)
{
    int ch, newTail, i, j;
    
    while ((ch = rxCheck(dev)) != -1) {
        switch (dev->messageState) {
        case WIFI_STATE_START:
            if (ch == CMD_START_BYTE) {
                dev->messageStart = dev->messageTail;
                newTail = (dev->messageTail + 1) % sizeof(dev->messageBuffer);
                if (newTail != dev->messageHead) {
                    dev->messageBuffer[dev->messageTail] = ch;
                    dev->messageTail = newTail;
                }
                else {
                    dbg("MSG: queue overflow\n");
                    return -1;
                }
                dev->messageState = WIFI_STATE_DATA;
            }
            else {
                // out of band data
                return -1;
            }
            break;
        case WIFI_STATE_DATA:
            newTail = (dev->messageTail + 1) % sizeof(dev->messageBuffer);
            if (newTail != dev->messageHead) {
                dev->messageBuffer[dev->messageTail] = ch;
                dev->messageTail = newTail;
            }
            else {
                dbg("MSG: queue overflow\n");
                return -1;
            }
            if (ch == '\r') {
#if 1
                {   char tmp[128];
                    i = dev->messageStart;
                    j = 0;
                    while (i != dev->messageTail) {
                        if (j < sizeof(tmp) - 1)
                            tmp[j++] = dev->messageBuffer[i];
                        i = (i + 1) % sizeof(dev->messageBuffer);
                    }
                    tmp[j] = '\0';
                    dbg("Got: %s\n", &tmp[1]);
                }
#endif
                dev->messageState = WIFI_STATE_START;
                i = (dev->messageStart + 1) % sizeof(dev->messageBuffer);
                if (dev->messageBuffer[i] == type) {
                    j = 0;
                    while (i != dev->messageTail) {
                        int ch = dev->messageBuffer[i];
                        i = (i + 1) % sizeof(dev->messageBuffer);
                        if (i != dev->messageTail && j < maxSize - 1)
                            buf[j++] = ch;
                    }
                    buf[j] = '\0';
                    dev->messageTail = dev->messageStart;
                    return j;
                }
            }
            break;
        default:
            dbg("MSG: internal error\n");
            return -1;
        }
    }

    return 0;
}

int sscpGetResponse(wifi *dev, char *buf, int maxSize)
{
    int ret;
    while ((ret = checkForMessage(dev, '=', buf, maxSize)) <= 0)
        ;
    return ret;
}

int sscpCheckForEvent(wifi *dev, char *buf, int maxSize)
{
    if (dev->messageHead != dev->messageTail) {
        int i = dev->messageHead;
        int j = 0;
        int ch;
        while (i != dev->messageTail) {
            ch = dev->messageBuffer[i];
            if (j < maxSize - 1)
                buf[j++] = ch;
            i = (i + 1) % sizeof(dev->messageBuffer);
            if (ch == '\r') {
                dev->messageHead = i;
                buf[j] = '\0';
                dbg("Got *QUEUED* event: %s\n", &buf[1]);
                return j;
            }   
        }
    }
    return checkForMessage(dev, '!', buf, maxSize);
}

#define DUMP_ARGS

#define SSCP_MAX_ARGS   16

void parseResponse(char *buf)
{
    char *argv[SSCP_MAX_ARGS + 1];
    char *p, *next;
    int argc;
    
    p = buf;
    argc = 0;
    
    if (*p) {
    
        while ((next = strchr(p, ',')) != NULL) {
            if (argc < SSCP_MAX_ARGS)
                argv[argc++] = p;
            *next++ = '\0';
            p = next;
        }
    
        if (argc < SSCP_MAX_ARGS)
            argv[argc++] = p;
    }
        
    argv[argc] = NULL;
        
#ifdef DUMP_ARGS
    int i;
    for (i = 0; i < argc; ++i)
        printf("argv[%d] = '%s'\n", i, argv[i]);
#endif
}


