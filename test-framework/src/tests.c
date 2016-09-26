#include "tests.h"

static int test_001(TestState *state);

void run_tests(TestState *parent, int selectedTest)
{
    TestState state;

    initState(&state, "Test", parent);
    state.selectedTest = selectedTest;

    if (startTest(&state, "the empty command")) {
        if (serialRequest(&state, ""))
            checkSerialResponse(&state, "=S,0");
    }

    if (startTest(&state, "an invalid command")) {
        if (serialRequest(&state, "FOO"))
            checkSerialResponse(&state, "=E,1");
    }

    if (state.ssid) {
        if (startTest(&state, "JOIN")) {
            if (serialRequest(&state, "JOIN:%s,%s", state.ssid, state.passwd))
                checkSerialResponse(&state, "=S,0");
        }
    }

    if (startTest(&state, "simple transaction")) {
        if (test_001(&state))
            passTest(&state, "");
        else
            failTest(&state, "");
    }

    testResults(&state);
}

static int test_001(TestState *state)
{
    TestState state2;
    int listener1, listener2, connection1, count, result;
    char response[1024];

    initState(&state2, "  Subtest", state);

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
            if (sendRequest(&state->moduleAddr, "POST", "/robot?gto=f", "") >= 0)
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

