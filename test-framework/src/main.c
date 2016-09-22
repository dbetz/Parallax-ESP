#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "cmd.h"
#include "serial.h"
#include "sock.h"

#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

int verbose = FALSE;

int serialRequest(wifi *dev, const char *fmt, ...);
int checkSerialResponse(wifi *dev, const char *fmt, ...);
int serialWaitResponse(wifi *dev, const char *idle, const char *fmt, ...);

int sendRequest(SOCKADDR_IN *addr, const char *method, const char *url, const char *body);
int receiveResponse(uint8_t *res, int resMax, int *pResult);
int parseBuffer(const char *buf, const char *fmt, ...);

static int parseBufferV(const char *buf, const char *fmt, va_list ap);

const char *testName = NULL;
int passCount = 0;
int failCount = 0;

#define startTest(name) testName = (name)

void passTest(const char *fmt, ...)
{
    va_list ap;
    printf("%s: PASSED", testName);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    ++passCount;
}

void failTest(const char *fmt, ...)
{
    va_list ap;
    printf("%s: FAILED", testName);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    ++failCount;
}

int main(int argc, char *argv[])
{
    char *serialDevice = "/dev/ttyUSB0";
    char *moduleAddress = "10.0.1.32";
    wifi dev;
    SOCKADDR_IN addr;
    char response[1024];
    int result, listener1, listener2, connection1, count;

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

    if (sendRequest(&addr, "GET", "/wx/setting?name=version", "") == -1)
        fprintf(stderr, "error: sendRequest failed\n");
    else if (receiveResponse((uint8_t *)response, sizeof(response), &result) == -1)
        fprintf(stderr, "error: receiveResponse failed\n");
    else
        printf("response: %d\n%s\n", result, response);

    startTest("Test 1");
    if (serialRequest(&dev, ""))
        checkSerialResponse(&dev, "=S,0");

    startTest("Test 2");
    if (serialRequest(&dev, "LISTEN:HTTP,/robot*"))
        checkSerialResponse(&dev, "=S,^i", &listener1);

    if (sendRequest(&addr, "POST", "/robot?gto=f", "") < 0)
        fprintf(stderr, "error: sendRequest failed\n");

    startTest("Test 3");
    do {
        serialRequest(&dev, "POLL");
    } while (!serialWaitResponse(&dev, "=N,0,0", "=P,^i,^i", &connection1, &listener2));

    startTest("Test 4");
    if (serialRequest(&dev, "PATH:%d", connection1))
        checkSerialResponse(&dev, "=S,/robot");

    startTest("Test 5");
    if (serialRequest(&dev, "ARG:%d,gto", connection1))
        checkSerialResponse(&dev, "=S,f");

    startTest("Test 6");
    if (serialRequest(&dev, "REPLY:%d,200", connection1))
        checkSerialResponse(&dev, "=S,^i", &count);

    if (receiveResponse((uint8_t *)response, sizeof(response), &result) == -1)
        fprintf(stderr, "error: receiveResponse failed\n");
    else
        printf("response: %d\n%s\n", result, response);

    sscpClose(&dev);

    printf("%d PASSED, %d FAILED\n", passCount, failCount);

    return 0;
}

int serialRequest(wifi *dev, const char *fmt, ...)
{
    va_list ap;
    int ret;
    
    va_start(ap, fmt);
    ret = sscpRequestV(dev, fmt, ap) >= 0;
    va_end(ap);

    if (!ret)
        failTest("");

    return ret;
}

int checkSerialResponse(wifi *dev, const char *fmt, ...)
{
    char response[1024];
    va_list ap;
    int ret;

    if (sscpGetResponse(dev, response, sizeof(response)) < 0)
        return FALSE;

    va_start(ap, fmt);
    ret = parseBufferV(response, fmt, ap) >= 0;
    va_end(ap);

    if (ret)
        passTest("");
    else
        failTest(": '%s'", response);

    return TRUE;
}

int serialWaitResponse(wifi *dev, const char *idle, const char *fmt, ...)
{
    char response[1024];
    va_list ap;
    int ret;

    if (sscpGetResponse(dev, response, sizeof(response)) >= 0) {
        if (parseBuffer(response, idle) == 0) {
            printf("%s: waiting...\n", testName);
            return FALSE;
        }
    }

    va_start(ap, fmt);
    ret = parseBufferV(response, fmt, ap) >= 0;
    va_end(ap);

    if (ret)
        passTest("");
    else
        failTest(": '%s'", response);

    return TRUE;
}

#define CONNECT_TIMEOUT 2000
#define RECEIVE_TIMEOUT 10000

SOCKET sock;

static void dumpHdr(const uint8_t *buf, int size);
static void dumpResponse(const uint8_t *buf, int size);

#define HDR_FMT "\
%s %s HTTP/1.1\r\n\
Content-Length: %d\r\n\
\r\n\
%s"

int sendRequest(SOCKADDR_IN *addr, const char *method, const char *url, const char *body)
{
    int bodySize, reqSize;
    char req[1024];

    bodySize = strlen(body);
    reqSize = snprintf(req, sizeof(req), HDR_FMT, method, url, bodySize, body);

    if (ConnectSocketTimeout(addr, CONNECT_TIMEOUT, &sock) != 0) {
        fprintf(stderr, "error: connect failed\n");
        return -1;
    }
    
    if (verbose) {
        printf("REQ: %d\n", reqSize);
        dumpHdr((uint8_t *)req, reqSize);
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
} State;

static int nextchar(State *state)
{
    return *state->p ? *state->p++ : EOF;
}

int parseBuffer(const char *buf, const char *fmt, ...)
{
    va_list ap;
    int ret;
    
    va_start(ap, fmt);
    ret = parseBufferV(buf, fmt, ap);
    va_end(ap);
    
    return ret;
}

static int parseBufferV(const char *buf, const char *fmt, va_list ap)
{
    State state = { .p = buf };
    const char *p = fmt;
    int ch, rch;

    rch = nextchar(&state);
        
    while ((ch = *p++) != '\0') {
    
        if (ch == '^') {
            switch (*p++) {
            case 'c':
                if (rch == EOF)
                    return -1;
                *va_arg(ap, char *) = rch;
                rch = nextchar(&state);
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
                }
                break;
            case '^':
                if (rch != '^')
                    return -1;
                rch = nextchar(&state);
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
            rch = nextchar(&state);
       }
    }

    return rch == EOF ? 0 : -1;
}

