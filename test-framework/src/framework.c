#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "framework.h"
#include "cmd.h"
#include "serial.h"
#include "sock.h"

#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

int testNumber = 0;
const char *testName = NULL;
int testPassed = FALSE;
int skipRemainingTests = FALSE;
int passCount = 0;
int failCount = 0;
int skipCount = 0;
int selectedTest = 0;

int verbose = FALSE;

static int parseBuffer(const char *buf, const char *fmt, ...);
static int parseBufferV(const char *buf, const char *fmt, va_list ap);

int startTest(const char *name)
{
    ++testNumber;
    if (selectedTest != 0 && testNumber != selectedTest)
        return FALSE;
printf("%s\n", name);
    testName = name;
    return TRUE;
}

void infoTest(const char *fmt, ...)
{
    va_list ap;
    printf("Test %d: ", testNumber);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

void passTest(const char *fmt, ...)
{
    va_list ap;
    printf("Test %d: PASSED", testNumber);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    testPassed = TRUE;
    ++passCount;
}

void failTest(const char *fmt, ...)
{
    va_list ap;
    printf("Test %d: FAILED", testNumber);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    testPassed = FALSE;
    ++failCount;
}

void beginGroup(void)
{
    skipRemainingTests = testPassed;
}

int skipTest(void)
{
    if (skipRemainingTests)
        return FALSE;
    infoTest("SKIPPED");
    ++skipCount;
    return TRUE;
}

void testResults(void)
{
    printf("%d PASSED, %d FAILED, %d SKIPPED, %d TOTAL\n", passCount, failCount, skipCount, passCount + failCount + skipCount);
}

int serialRequest(wifi *dev, const char *fmt, ...)
{
    va_list ap;
    int ret;
    
    va_start(ap, fmt);
    ret = sscpRequestV(dev, fmt, ap);
    va_end(ap);

    if (ret < 0)
        failTest(": sending request");

    return ret >= 0;
}

void checkSerialResponse(wifi *dev, const char *fmt, ...)
{
    char response[1024];
    va_list ap;
    int ret;

    if (sscpGetResponse(dev, response, sizeof(response)) < 0) {
        failTest(": receiving response");
        return;
    }

    va_start(ap, fmt);
    ret = parseBufferV(response, fmt, ap);
    va_end(ap);

    if (ret >= 0)
        passTest("");
    else
        failTest(": '%s'", response);
}

int waitAndCheckSerialResponse(wifi *dev, const char *idle, const char *fmt, ...)
{
    char response[1024];
    va_list ap;
    int ret;

    if (sscpGetResponse(dev, response, sizeof(response)) < 0) {
        failTest(": receiving response");
        return TRUE;
    }
    
    if (parseBuffer(response, idle) == 0) {
        infoTest("waiting...");
        return FALSE;
    }

    va_start(ap, fmt);
    ret = parseBufferV(response, fmt, ap);
    va_end(ap);

    if (ret >= 0)
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

static int parseBuffer(const char *buf, const char *fmt, ...)
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

