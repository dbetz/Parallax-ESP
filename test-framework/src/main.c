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

static int test_001(wifi *dev);

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

    if (startTest("the empty command")) {
        if (serialRequest(&dev, ""))
            checkSerialResponse(&dev, "=S,0");
    }

    if (startTest("an invalid command")) {
        if (serialRequest(&dev, "FOO"))
            checkSerialResponse(&dev, "=E,1");
    }

    if (GetInternetAddress(moduleAddress, 80, &moduleAddr) != 0) {
        fprintf(stderr, "error: failed to parse IP address '%s'\n", moduleAddress);
        return 1;
    }

    test_001(&dev);

    sscpClose(&dev);

    testResults();

    return 0;
}

static int test_001(wifi *dev)
{
    int listener1, listener2, connection1, count, result;
    char response[1024];

    if (startTest("LISTEN")) {
        if (serialRequest(dev, "LISTEN:HTTP,/robot*"))
            checkSerialResponse(dev, "=S,^i", &listener1);
    }

    beginGroup();

    if (startTest("PATH of a listener")) {
        if (!skipTest() && serialRequest(dev, "PATH:%d", listener1))
            checkSerialResponse(dev, "=S,/robot*");
    }

    if (startTest("Send request to WX module")) {
        if (!skipTest() && sendRequest(&moduleAddr, "POST", "/robot?gto=f", "") < 0)
            fprintf(stderr, "error: sendRequest failed\n");
    }

    if (startTest("POLL for incoming POST request")) {
        if (!skipTest()) {
            do {
                if (!serialRequest(dev, "POLL"))
                    break;
            } while (!waitAndCheckSerialResponse(dev, "=N,0,0", "=P,^i,^i", &connection1, &listener2));
        }
    }

    beginGroup();

    if (startTest("listener handle returned by POLL")) {
        if (listener1 == listener2)
            passTest("");
        else
            failTest(": got %d", listener2);
    }

    if (startTest("PATH of a connection")) {
        if (!skipTest() && serialRequest(dev, "PATH:%d", connection1))
            checkSerialResponse(dev, "=S,/robot");
    }

    if (startTest("ARG")) {
        if (!skipTest() && serialRequest(dev, "ARG:%d,gto", connection1))
            checkSerialResponse(dev, "=S,f");
    }

    if (startTest("REPLY")) {
        if (!skipTest() && serialRequest(dev, "REPLY:%d,200", connection1))
            checkSerialResponse(dev, "=S,^i", &count);
    }

    if (startTest("POLL for send complete")) {
        if (!skipTest()) {
            do {
                if (!serialRequest(dev, "POLL"))
                    break;
            } while (!waitAndCheckSerialResponse(dev, "=N,0,0", "=S,^i,0", &count, &listener2));
        }
    }

    if (startTest("Receive response from WX")) {
        if (!skipTest()) {
            if (receiveResponse((uint8_t *)response, sizeof(response), &result) == -1)
                fprintf(stderr, "error: receiveResponse failed\n");
//            else
//                printf("response: %d\n%s\n", result, response);
        }
    }

    if (startTest("CLOSE")) {
        if (!skipTest() && serialRequest(dev, "CLOSE:%d", listener1))
            checkSerialResponse(dev, "=S,0");
    }

    return 1;
}

