#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "tests.h"
#include "framework.h"
#include "cmd.h"
#include "serial.h"
#include "sock.h"

static void usage(const char *progname)
{
printf("\
usage: %s [options] [<file>]\n\
\n\
options:\n\
    -i <ip-addr>    IP address of the Wi-Fi module\n\
    -p <port>       serial port connected to DI/DO on the Wi-Fi module\n\
    -s <ssid>       specify an AP SSID\n\
    -t <number>     select a specific test\n\
    -x <passwd>     specify an AP password\n\
    -?              display a usage message and exit\n\
", progname);
    exit(1);
}

int main(int argc, char *argv[])
{
    char *serialDevice = "/dev/ttyUSB0";
    char *moduleAddress = "10.0.1.32";
    TestState globalState;
    int selectedTest = 0;
    wifi dev;
    int i;

    initState(&globalState, "", NULL);
    globalState.passwd = "";

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
            case 's':   // specify an AP ssid
                if (argv[i][2])
                    globalState.ssid = &argv[i][2];
                else if (++i < argc)
                    globalState.ssid = argv[i];
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
            case 'x':   // specify an AP password
                if (argv[i][2])
                    globalState.passwd = &argv[i][2];
                else if (++i < argc)
                    globalState.passwd = argv[i];
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

    if (globalState.ssid)
        printf("[ using: SSID '%s', PASSWD '%s' ]\n", globalState.ssid, globalState.passwd);

    if (GetInternetAddress(moduleAddress, 80, &globalState.moduleAddr) != 0) {
        fprintf(stderr, "error: failed to parse IP address '%s'\n", moduleAddress);
        return 1;
    }

    if (sscpOpen(&dev, serialDevice) != 0) {
        fprintf(stderr, "error: failed to open '%s'\n", serialDevice);
        return 1;
    }
    globalState.dev = &dev;

    run_tests(&globalState, selectedTest);

    sscpClose(globalState.dev);

    return 0;
}

