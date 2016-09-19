#include <stdio.h>
#include "cmd.h"
#include "serial.h"
#include "sock.h"

int main(int argc, char *argv[])
{
    char *serialDevice = "/dev/ttyUSB0";
    wifi dev;

    printf("Welcome to the WX test framework!\n");

    if (argc > 1)
        serialDevice = argv[1];

    if (sscpOpen(&dev, serialDevice) != 0) {
        fprintf(stderr, "error: failed to open '%s'\n", serialDevice);
        return 1;
    }

    char response[128];
    sscpRequest(&dev, "LISTEN:HTTP,/robot*");
    if (sscpGetResponse(&dev, response, sizeof(response)) > 0) {
        printf("Response: '%s'\n", response);
        parseResponse(response);
    }

    sscpClose(&dev);

    return 0;
}

#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

#define CONNECT_TIMEOUT 2000

SOCKADDR_IN m_httpAddr;
int verbose = 1;

void dumpHdr(const uint8_t *buf, int size);
void dumpResponse(const uint8_t *buf, int size);

int sendRequest(uint8_t *req, int reqSize, uint8_t *res, int resMax, int *pResult)
{
    SOCKET sock;
    char buf[80];
    int cnt;
    
    if (ConnectSocketTimeout(&m_httpAddr, CONNECT_TIMEOUT, &sock) != 0) {
        printf("error: connect failed\n");
        return -1;
    }
    
    if (verbose) {
        printf("REQ: %d\n", reqSize);
        dumpHdr(req, reqSize);
    }
    
    if (SendSocketData(sock, req, reqSize) != reqSize) {
        printf("error: send request failed\n");
        CloseSocket(sock);
        return -1;
    }
    
    cnt = ReceiveSocketDataTimeout(sock, res, resMax, 10000);
    CloseSocket(sock);

    if (cnt == -1) {
        printf("error: receive response failed\n");
        return -1;
    }
    
    if (verbose) {
        printf("RES: %d\n", cnt);
        dumpResponse(res, cnt);
    }
    
    if (sscanf((char *)res, "%s %d", buf, pResult) != 2)
        return -1;
        
    return cnt;
}

void dumpHdr(const uint8_t *buf, int size)
{
    int startOfLine = TRUE;
    const uint8_t *p = buf;
    while (p < buf + size) {
        if (*p == '\r') {
            if (startOfLine)
                break;
            startOfLine = TRUE;
            putchar('\n');
        }
        else if (*p != '\n') {
            startOfLine = FALSE;
            putchar(*p);
        }
        ++p;
    }
    putchar('\n');
}

void dumpResponse(const uint8_t *buf, int size)
{
    int startOfLine = TRUE;
    const uint8_t *p = buf;
    const uint8_t *save;
    int cnt;
    
    while (p < buf + size) {
        if (*p == '\r') {
            if (startOfLine) {
                ++p;
                if (*p == '\n')
                    ++p;
                break;
            }
            startOfLine = TRUE;
            putchar('\n');
        }
        else if (*p != '\n') {
            startOfLine = FALSE;
            putchar(*p);
        }
        ++p;
    }
    putchar('\n');
    
    save = p;
    while (p < buf + size) {
        if (*p == '\r')
            putchar('\n');
        else if (*p != '\n')
            putchar(*p);
        ++p;
    }

    p = save;
    cnt = 0;
    while (p < buf + size) {
        printf("%02x ", *p++);
        if ((++cnt % 16) == 0)
            putchar('\n');
    }
    if ((cnt % 16) != 0)
        putchar('\n');
}

