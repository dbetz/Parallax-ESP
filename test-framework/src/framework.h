#ifndef __FRAMEWORK_H__
#define __FRAMEWORK_H__

#include <stdint.h>
#include "cmd.h"
#include "sock.h"

extern int selectedTest;
extern int verbose;

int startTest(const char *name);
void infoTest(const char *fmt, ...);
void passTest(const char *fmt, ...);
void failTest(const char *fmt, ...);
void beginGroup(void);
int skipTest(void);
void testResults(void);

/* serial requests */
int serialRequest(wifi *dev, const char *fmt, ...);
void checkSerialResponse(wifi *dev, const char *fmt, ...);
int waitAndCheckSerialResponse(wifi *dev, const char *idle, const char *fmt, ...);

/* http requests */
int sendRequest(SOCKADDR_IN *addr, const char *method, const char *url, const char *body);
int receiveResponse(uint8_t *res, int resMax, int *pResult);

#endif


