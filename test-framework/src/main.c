#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "framework.h"
#include "cmd.h"
#include "serial.h"
#include "sock.h"

SOCKADDR_IN moduleAddr;

static int test_001(TestState *state);

static void usage(const char *progname)
{
printf("\
usage: %s [options] [<file>]\n\
\n\
options:\n\
    -i <ip-addr>    IP address of the Wi-Fi module\n\
    -p <port>       serial port connected to DI/DO on the Wi-Fi module\n\
    -t <number>     select a specific test\n\
    -?              display a usage message and exit\n\
", progname);
    exit(1);
}

int main(int argc, char *argv[])
{
    char *serialDevice = "/dev/ttyUSB0";
    char *moduleAddress = "10.0.1.32";
    int selectedTest = 0;
    TestState state;
    wifi dev;
    int i;

    /* get the arguments */
    for (i = 1; i < argc; ++i) {

        /* handle switches */
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'i':   // set the ip address
                if (argv[i][2])
                    moduleAddress = &argv[i][2];
                else if (++i < argc)
                    moduleAddress = argv[i];
                else
                    usage(argv[0]);
                break;
            case 'p':   // select a serial port
                if (argv[i][2])
                    serialDevice = &argv[i][2];
                else if (++i < argc)
                    serialDevice = argv[i];
                else
                    usage(argv[0]);
                break;
            case 't':   // select a target board
                if (argv[i][2])
                    selectedTest = atoi(&argv[i][2]);
                else if (++i < argc)
                    selectedTest = atoi(argv[i]);
                else
                    usage(argv[0]);
                break;
            default:
                usage(argv[0]);
                break;
            }
        }
        
        /* remember the file to load */
        else {
            usage(argv[0]);
        }
    }

    printf("Welcome to the WX test framework!\n");

    if (sscpOpen(&dev, serialDevice) != 0) {
        fprintf(stderr, "error: failed to open '%s'\n", serialDevice);
        return 1;
    }

    initState(&state, &dev);
    state.selectedTest = selectedTest;

    if (startTest(&state, "the empty command")) {
        if (serialRequest(&state, ""))
            checkSerialResponse(&state, "=S,0");
    }

    if (startTest(&state, "an invalid command")) {
        if (serialRequest(&state, "FOO"))
            checkSerialResponse(&state, "=E,1");
    }

    if (GetInternetAddress(moduleAddress, 80, &moduleAddr) != 0) {
        fprintf(stderr, "error: failed to parse IP address '%s'\n", moduleAddress);
        return 1;
    }

    if (startTest(&state, "simple transaction")) {
        if (test_001(&state))
            passTest(&state, "");
        else
            failTest(&state, "");
    }

    testResults(&state);

    sscpClose(state.dev);

    return 0;
}

static int test_001(TestState *state)
{
    TestState state2;
    int listener1, listener2, connection1, count, result;
    char response[1024];

    initState(&state2, state->dev);

    if (startTest(&state2, "LISTEN")) {
        if (serialRequest(&state2, "LISTEN:HTTP,/robot*"))
            checkSerialResponse(&state2, "=S,^i", &listener1);
    }

    beginGroup(&state2);

    if (startTest(&state2, "PATH of a listener")) {
        if (!skipTest(&state2) && serialRequest(&state2, "PATH:%d", listener1))
            checkSerialResponse(&state2, "=S,/robot*");
    }

    if (startTest(&state2, "Send request to WX module")) {
        if (!skipTest(&state2)) {
            if (sendRequest(&moduleAddr, "POST", "/robot?gto=f", "") >= 0)
                passTest(&state2, "");
            else
                failTest(&state2, "");
        }
    }

    if (startTest(&state2, "POLL for incoming POST request")) {
        if (!skipTest(&state2)) {
            do {
                if (!serialRequest(&state2, "POLL"))
                    break;
            } while (!waitAndCheckSerialResponse(&state2, "=N,0,0", "=P,^i,^i", &connection1, &listener2));
        }
    }

    beginGroup(&state2);

    if (startTest(&state2, "listener handle returned by POLL")) {
        if (listener1 == listener2)
            passTest(&state2, "");
        else
            failTest(&state2, ": got %d", listener2);
    }

    if (startTest(&state2, "PATH of a connection")) {
        if (!skipTest(&state2) && serialRequest(&state2, "PATH:%d", connection1))
            checkSerialResponse(&state2, "=S,/robot");
    }

    if (startTest(&state2, "ARG")) {
        if (!skipTest(&state2) && serialRequest(&state2, "ARG:%d,gto", connection1))
            checkSerialResponse(&state2, "=S,f");
    }

    if (startTest(&state2, "REPLY")) {
        if (!skipTest(&state2) && serialRequest(&state2, "REPLY:%d,200", connection1))
            checkSerialResponse(&state2, "=S,^i", &count);
    }

    if (startTest(&state2, "POLL for send complete")) {
        if (!skipTest(&state2)) {
            do {
                if (!serialRequest(&state2, "POLL"))
                    break;
            } while (!waitAndCheckSerialResponse(&state2, "=N,0,0", "=S,^i,0", &count, &listener2));
        }
    }

    if (startTest(&state2, "Receive response from WX")) {
        if (!skipTest(&state2)) {
            if (receiveResponse((uint8_t *)response, sizeof(response), &result) >= 0)
                passTest(&state2, "");
            else
                failTest(&state2, "");
        }
    }

    if (startTest(&state2, "CLOSE")) {
        if (!skipTest(&state2) && serialRequest(&state2, "CLOSE:%d", listener1))
            checkSerialResponse(&state2, "=S,0");
    }

    testResults(&state2);

    return state2.testPassed;
}

