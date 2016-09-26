#ifndef __FRAMEWORK_H__
#define __FRAMEWORK_H__

#include <stdint.h>
#include "cmd.h"
#include "sock.h"

extern int verbose;

typedef struct {
    wifi *dev;
    int testNumber;
    const char *testName;
    int testPassed;
    int skipRemainingTests;
    int passCount;
    int failCount;
    int skipCount;
    int selectedTest;
} TestState;

void initState(TestState *state, wifi *dev);
int startTest(TestState *state, const char *name);
void infoTest(TestState *state, const char *fmt, ...);
void passTest(TestState *state, const char *fmt, ...);
void failTest(TestState *state, const char *fmt, ...);
void beginGroup(TestState *state);
int skipTest(TestState *state);
void testResults(TestState *state);

/* serial requests */
int serialRequest(TestState *state, const char *fmt, ...);
void checkSerialResponse(TestState *state, const char *fmt, ...);
int waitAndCheckSerialResponse(TestState *state, const char *idle, const char *fmt, ...);

/* http requests */
int sendRequest(SOCKADDR_IN *addr, const char *method, const char *url, const char *body);
int receiveResponse(uint8_t *res, int resMax, int *pResult);

#endif


