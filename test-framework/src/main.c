#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "cmd.h"
#include "serial.h"
#include "sock.h"

#define REQUEST "\
GET /wx/setting?name=version HTTP/1.1\r\n\
\r\n\
"

int sendRequest(SOCKADDR_IN *addr, char *req);
int receiveResponse(uint8_t *res, int resMax, int *pResult);
int parseBuffer(const char *buf, const char *fmt, ...);

int main(int argc, char *argv[])
{
    char *serialDevice = "/dev/ttyUSB0";
    char *moduleAddress = "10.0.1.32";
    wifi dev;
    SOCKADDR_IN addr;
    char response[1024];
    int result, listener1, listener2, connection1, connection2, count;

    printf("Welcome to the WX test framework!\n");

    if (argc > 1)
        serialDevice = argv[1];
    if (argc > 2)
        moduleAddress = argv[2];

    if (sscpOpen(&dev, serialDevice) != 0) {
        fprintf(stderr, "error: failed to open '%s'\n", serialDevice);
        return 1;
    }

    if (GetInternetAddress(moduleAddress, 80, &addr) != 0) {
        fprintf(stderr, "error: failed to parse IP address '%s'\n", moduleAddress);
        return 1;
    }

    if (sendRequest(&addr, REQUEST) == -1)
        fprintf(stderr, "error: sendRequest failed\n");
    else if (receiveResponse((uint8_t *)response, sizeof(response), &result) == -1)
        fprintf(stderr, "error: receiveResponse failed\n");
    else
        printf("response: %d\n%s", result, response);

    sscpRequest(&dev, "");
    if (sscpGetResponse(&dev, response, sizeof(response)) >= 0) {
        if (parseBuffer(response, "=S,0", &result) == 0)
            printf("PASSED\n");
        else
            printf("FAILED: '%s'\n", response);
    }

    sscpRequest(&dev, "LISTEN:HTTP,/robot*");
    if (sscpGetResponse(&dev, response, sizeof(response)) >= 0) {
        if (parseBuffer(response, "=S,^i", &listener1) == 0)
            printf("PASSED, listener %d\n", listener1);
        else
            printf("FAILED: '%s'\n", response);
    }

    if (sendRequest(&addr, "\
POST /robot?gto=f HTTP/1.1\r\n\
\r\n\
") == -1)
        fprintf(stderr, "error: sendRequest failed\n");

    for (;;) {
        sscpRequest(&dev, "POLL");
        if (sscpGetResponse(&dev, response, sizeof(response)) >= 0)
            if (parseBuffer(response, "=N,0,0") != 0)
                break;
            printf("Waiting...\n");
    }
    if (parseBuffer(response, "=P,^i,^i", &connection1, &listener2) == 0)
        printf("PASSED, connection %d, listener %d\n", connection1, listener2);
    else
        printf("FAILED: '%s'\n", response);

    sscpRequest(&dev, "PATH:%d", connection1);
    if (sscpGetResponse(&dev, response, sizeof(response)) >= 0) {
        if (parseBuffer(response, "=S,/robot") == 0)
            printf("PASSED\n");
        else
            printf("FAILED: '%s'\n", response);
    }

    sscpRequest(&dev, "ARG:%d,gto", connection1);
    if (sscpGetResponse(&dev, response, sizeof(response)) >= 0) {
        if (parseBuffer(response, "=S,f") == 0)
            printf("PASSED\n");
        else
            printf("FAILED: '%s'\n", response);
    }

    sscpRequest(&dev, "REPLY:%d,200", connection1);
    if (sscpGetResponse(&dev, response, sizeof(response)) >= 0) {
        if (parseBuffer(response, "=S,^i", &count) == 0)
            printf("PASSED, count %d\n", count);
        else
            printf("FAILED: '%s'\n", response);
    }

    if (receiveResponse((uint8_t *)response, sizeof(response), &result) == -1)
        fprintf(stderr, "error: receiveResponse failed\n");
    else
        printf("response: %d\n%s", result, response);

    sscpClose(&dev);

    return 0;
}

#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

#define CONNECT_TIMEOUT 2000
#define RECEIVE_TIMEOUT 10000

int verbose = 1;
SOCKET sock;

static void dumpHdr(const uint8_t *buf, int size);
static void dumpResponse(const uint8_t *buf, int size);

int sendRequest(SOCKADDR_IN *addr, char *req)
{
    int reqSize = strlen(req);

    if (ConnectSocketTimeout(addr, CONNECT_TIMEOUT, &sock) != 0) {
        fprintf(stderr, "error: connect failed\n");
        return -1;
    }
    
    if (verbose) {
        printf("REQ: %d\n", reqSize);
        dumpHdr(req, reqSize);
    }
    
    if (SendSocketData(sock, req, reqSize) != reqSize) {
        fprintf(stderr, "error: send request failed\n");
        CloseSocket(sock);
        return -1;
    }
    
    return reqSize;
}

int receiveResponse(uint8_t *res, int resMax, int *pResult)
{
    char buf[80];
    int cnt;

    memset(res, 0, resMax);
    cnt = ReceiveSocketDataTimeout(sock, res, resMax, RECEIVE_TIMEOUT);
    CloseSocket(sock);

    if (cnt == -1) {
        fprintf(stderr, "error: receive response failed\n");
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

static void dumpHdr(const uint8_t *buf, int size)
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

static void dumpResponse(const uint8_t *buf, int size)
{
    int startOfLine = TRUE;
    const uint8_t *p = buf;
    const uint8_t *save;
    int cnt, lastch;
    
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
    lastch = '\r';
    while (p < buf + size) {
        if ((lastch = *p) == '\r')
            putchar('\n');
        else if (*p != '\n')
            putchar(*p);
        ++p;
    }
    if (lastch != '\r')
        putchar('\n');

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
typedef struct {
    const char *p;
    int savedChar;
} State;

static int parseBuffer1(const char *buf, const char *fmt, va_list ap);

static int nextchar(State *state)
{
    int ch;
    if ((ch = state->savedChar) != EOF)
        state->savedChar = EOF;
    else
        ch = *state->p ? *state->p++ : EOF;
    return ch;
}

static void ungetchar(State *state, int ch)
{
    state->savedChar = ch;
}

int parseBuffer(const char *buf, const char *fmt, ...)
{
    va_list ap;
    int ret;
    
    va_start(ap, fmt);
    ret = parseBuffer1(buf, fmt, ap);
    va_end(ap);
    
    return ret;
}

static int parseBuffer1(const char *buf, const char *fmt, va_list ap)
{
    State state = { .p = buf, .savedChar = EOF };
    const char *p = fmt;
    int ch, rch;

    while ((ch = *p++) != '\0') {
    
        rch = nextchar(&state);
        
        if (ch == '^') {
            switch (*p++) {
            case 'c':
                if (rch == EOF)
                    return -1;
                *va_arg(ap, char *) = rch;
                break;
            case 'i':
                {
                    int value = 0;
                    int sign = 1;
                    if (rch == EOF)
                        return -1;
                    if (rch == '-') {
                        if ((rch = nextchar(&state)) == EOF)
                            return -1;
                        sign = -1;
                    }
                    if (!isdigit(rch))
                        return -1;
                    do {
                        value = value * 10 + rch - '0';
                    } while ((rch = nextchar(&state)) != EOF && isdigit(rch));
                    *va_arg(ap, int *) = value * sign;
                    ungetchar(&state, rch);
                }
                break;
            case 's':
                {
                    int term = *p;
                    char *buf = va_arg(ap, char *);
                    int len = va_arg(ap, int);
                    while (rch != EOF && rch != term) {
                        if (len > 1) {
                            *buf++ = rch;
                            --len;
                        }
                        rch = nextchar(&state);
                    }
                    *buf = '\0';
                    ungetchar(&state, rch);
                }
                break;
            case '^':
                if (rch != '^')
                    return -1;
                break;
            case '\0':
                // fall through
            default:
                return -1;
            }
        }
        
        else {
            if (rch != ch)
                return -1;
       }
    }

    return 0;
}

