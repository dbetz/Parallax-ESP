#include <esp8266.h>
#include <osapi.h>
#include "config.h"
#include "cgi.h"
#include "cgiprop.h"
#include "proploader.h"
#include "serbridge.h"
#include "sscp.h"
#include "uart.h"
#include "roffs.h"

//#define STATE_DEBUG

#define MAX_SENDBUFF_LEN 2600

void ICACHE_FLASH_ATTR httpdSendResponse(HttpdConnData *connData, int code, char *message, int len)
{
    char sendBuff[MAX_SENDBUFF_LEN];
    httpdSetSendBuffer(connData, sendBuff, sizeof(sendBuff));
    httpdStartResponse(connData, code);
    httpdEndHeaders(connData);
    httpdSend(connData, message, len);
    httpdFlushSendBuffer(connData);
    connData->cgi = NULL;
}

static ETSTimer resetButtonTimer;
static int resetButtonState;
static int resetButtonCount;

static void startLoading(PropellerConnection *connection, const uint8_t *image, int imageSize);
static void finishLoading(PropellerConnection *connection);
static void abortLoading(PropellerConnection *connection);
static void resetButtonTimerCallback(void *data);
static void armTimer(PropellerConnection *connection, int delay);
static void timerCallback(void *data);
static void readCallback(char *buf, short length);

/* the order here must match the definition of LoadState in proploader.h */
static const char * ICACHE_RODATA_ATTR stateNames[] = {
    "Idle",
    "Reset",
    "TxHandshake",
    "RxHandshakeStart",
    "RxHandshake",
    "LoadContinue",
    "VerifyChecksum",
    "StartAck"
};

static const ICACHE_FLASH_ATTR char *stateName(LoadState state)
{
    return state >= 0 && state < stMAX ? stateNames[state] : "Unknown";
}

static int8_t ICACHE_FLASH_ATTR getIntArg(HttpdConnData *connData, char *name, int *pValue)
{
  char buf[16];
  int len = httpdFindArg(connData->getArgs, name, buf, sizeof(buf));
  if (len < 0) return 0; // not found, skip
  *pValue = atoi(buf);
  return 1;
}

// this is statically allocated because the serial read callback has no context parameter
PropellerConnection myConnection;

int ICACHE_FLASH_ATTR cgiPropInit()
{
    memset(&myConnection, 0, sizeof(PropellerConnection));
    myConnection.state = stIdle;
    resetButtonState = 1;
    resetButtonCount = 0;
    gpio_output_set(0, 0, 0, 1 << RESET_BUTTON_PIN);
    os_timer_setfn(&resetButtonTimer, resetButtonTimerCallback, 0);
    os_timer_arm(&resetButtonTimer, RESET_BUTTON_SAMPLE_INTERVAL, 1);

    int ret;
    if ((ret = roffs_mount(FLASH_FILESYSTEM_BASE)) != 0) {
        os_printf("Mounting flash filesystem failed: %d\n", ret);
        return 0;
    }
    os_printf("Flash filesystem mounted!\n");

    return 1;
}

int ICACHE_FLASH_ATTR cgiPropBaudRate(HttpdConnData *connData)
{
    char response[32];
    int baudRate;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;

    // check for GET
    if (connData->requestType == HTTPD_METHOD_GET)
        os_sprintf(response, "%d", uart0_baudRate);

    // only other option is POST
    else {

        if (!getIntArg(connData, "baud-rate", &baudRate)) {
            httpdSendResponse(connData, 400, "No baud-rate specified\r\n", -1);
            return HTTPD_CGI_DONE;
        }

        DBG("set-baud-rate: baud-rate %d\n", baudRate);

        uart0_baud(baudRate);
        os_strcpy(response, "");
    }

    httpdStartResponse(connData, 200);
    httpdEndHeaders(connData);
    httpdSend(connData, response, -1);

    return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR cgiPropEnableSerialProtocol(HttpdConnData *connData)
{
    char response[32];
    int enable;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;

    // check for GET
    if (connData->requestType == HTTPD_METHOD_GET)
        os_strcpy(response, sscp_isEnabled() ? "1" : "0");

    // only other option is POST
    else {

        if (!getIntArg(connData, "enable", &enable)) {
            httpdSendResponse(connData, 400, "No enable specified\r\n", -1);
            return HTTPD_CGI_DONE;
        }

        DBG("enable-serial-protocol: enable %d\n", enable);

        sscp_enable(enable);
        os_strcpy(response, "");
    }

    httpdStartResponse(connData, 200);
    httpdEndHeaders(connData);
    httpdSend(connData, response, -1);

    return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR cgiPropLoad(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;

    if (connection->state != stIdle) {
        char buf[128];
        os_sprintf(buf, "Transfer already in progress: state %s\r\n", stateName(connection->state));
        httpdSendResponse(connData, 400, buf, -1);
        return HTTPD_CGI_DONE;
    }
    connData->cgiData = connection;
    connection->connData = connData;

    os_timer_setfn(&connection->timer, timerCallback, connection);
    
    if (connData->post->len == 0) {
        httpdSendResponse(connData, 400, "No data\r\n", -1);
        abortLoading(connection);
        return HTTPD_CGI_DONE;
    }
    else if (connData->post->buffLen != connData->post->len) {
        httpdSendResponse(connData, 400, "Data too large\r\n", -1);
        return HTTPD_CGI_DONE;
    }
    
    if (!getIntArg(connData, "baud-rate", &connection->baudRate))
        connection->baudRate = flashConfig.baud_rate;
    if (!getIntArg(connData, "final-baud-rate", &connection->finalBaudRate))
        connection->finalBaudRate = connection->baudRate;
    if (!getIntArg(connData, "reset-pin", &connection->resetPin))
        connection->resetPin = flashConfig.reset_pin;
    if (!getIntArg(connData, "response-size", &connection->responseSize))
        connection->responseSize = 0;
    if (!getIntArg(connData, "response-timeout", &connection->responseTimeout))
        connection->responseTimeout = 1000;
    
    DBG("load: size %d, baud-rate %d, final-baud-rate %d, reset-pin %d\n", connData->post->buffLen, connection->baudRate, connection->finalBaudRate, connection->resetPin);
    if (connection->responseSize > 0)
        DBG("  responseSize %d, responseTimeout %d\n", connection->responseSize, connection->responseTimeout);

    connection->file = NULL;
    startLoading(connection, (uint8_t *)connData->post->buff, connData->post->buffLen);

    return HTTPD_CGI_MORE;
}

int ICACHE_FLASH_ATTR cgiPropLoadFile(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    char fileName[128];
    int fileSize = 0;
    
    // check for the cleanup call
    if (connData->conn == NULL) {
        if (connection->file) {
            roffs_close(connection->file);
            connection->file = NULL;
        }
        return HTTPD_CGI_DONE;
    }

    if (connection->state != stIdle) {
        char buf[128];
        os_sprintf(buf, "Transfer already in progress: state %s\r\n", stateName(connection->state));
        httpdSendResponse(connData, 400, buf, -1);
        return HTTPD_CGI_DONE;
    }
    connData->cgiData = connection;
    connection->connData = connData;
    
    os_timer_setfn(&connection->timer, timerCallback, connection);
    
    if (httpdFindArg(connData->getArgs, "file", fileName, sizeof(fileName)) < 0) {
        httpdSendResponse(connData, 400, "Missing file argument\r\n", -1);
        return HTTPD_CGI_DONE;
    }

    if (!(connection->file = roffs_open(fileName))) {
        httpdSendResponse(connData, 400, "File not found\r\n", -1);
        return HTTPD_CGI_DONE;
    }
    fileSize = roffs_file_size(connection->file);

    if (!getIntArg(connData, "baud-rate", &connection->baudRate))
        connection->baudRate = flashConfig.baud_rate;
    if (!getIntArg(connData, "final-baud-rate", &connection->finalBaudRate))
        connection->finalBaudRate = connection->baudRate;
    if (!getIntArg(connData, "reset-pin", &connection->resetPin))
        connection->resetPin = flashConfig.reset_pin;
    
    DBG("load-file: file %s, size %d, baud-rate %d, final-baud-rate %d, reset-pin %d\n", fileName, fileSize, connection->baudRate, connection->finalBaudRate, connection->resetPin);

    startLoading(connection, NULL, fileSize);

    return HTTPD_CGI_MORE;
}

int ICACHE_FLASH_ATTR cgiPropReset(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;

    if (connection->state != stIdle) {
        char buf[128];
        os_sprintf(buf, "Transfer already in progress: state %s\r\n", stateName(connection->state));
        httpdSendResponse(connData, 400, buf, -1);
        return HTTPD_CGI_DONE;
    }
    connData->cgiData = connection;
    connection->connData = connData;

    // turn off SSCP during loading
    sscp_enable(0);

    os_timer_setfn(&connection->timer, timerCallback, connection);
    
    if (!getIntArg(connData, "reset-pin", &connection->resetPin))
        connection->resetPin = flashConfig.reset_pin;

    DBG("reset: reset-pin %d\n", connection->resetPin);

    connection->image = NULL;

    GPIO_OUTPUT_SET(connection->resetPin, 0);
    connection->state = stReset;
    
    armTimer(connection, RESET_DELAY_1);

    return HTTPD_CGI_MORE;
}

static void ICACHE_FLASH_ATTR startLoading(PropellerConnection *connection, const uint8_t *image, int imageSize)
{
    connection->image = image;
    connection->imageSize = imageSize;
    
    uart0_baud(connection->baudRate);

    GPIO_OUTPUT_SET(connection->resetPin, 0);
    armTimer(connection, RESET_DELAY_1);
    connection->state = stReset;
}

static void ICACHE_FLASH_ATTR finishLoading(PropellerConnection *connection)
{
    if (connection->finalBaudRate != connection->baudRate);
        uart0_baud(connection->finalBaudRate);
    programmingCB = NULL;
    myConnection.state = stIdle;
}

static void ICACHE_FLASH_ATTR abortLoading(PropellerConnection *connection)
{
    programmingCB = NULL;
    myConnection.state = stIdle;
}

static void ICACHE_FLASH_ATTR resetButtonTimerCallback(void *data)
{
    static int previousState = 1;
    static int matchingSampleCount = 0;
    static int buttonPressCount = 0;
    static uint32_t lastButtonTime;
    int newState = GPIO_INPUT_GET(RESET_BUTTON_PIN);
    if (newState != previousState)
        matchingSampleCount = 0;
    else if (matchingSampleCount < RESET_BUTTON_THRESHOLD) {
        if (++matchingSampleCount == RESET_BUTTON_THRESHOLD) {
            if (newState != resetButtonState) {
                resetButtonState = newState;
                if (resetButtonState == 0) {
                    uint32_t buttonTime = system_get_time() / 1000;
                    //os_printf("Reset button press: count %d, last %u, this %u\n", buttonPressCount, (unsigned)lastButtonTime, (unsigned)buttonTime);
                    if (buttonPressCount == 0 || buttonTime - lastButtonTime > RESET_BUTTON_PRESS_DELTA)
                        buttonPressCount = 1;
                    else if (++buttonPressCount == RESET_BUTTON_PRESS_COUNT) {
                        os_printf("Entering STA+AP mode\n");
                        wifi_set_opmode(STATIONAP_MODE);
                        buttonPressCount = 0;
                    }
                    lastButtonTime = buttonTime;
                }
            }
        }
    }
    previousState = newState;
}

static void ICACHE_FLASH_ATTR armTimer(PropellerConnection *connection, int delay)
{
    os_timer_disarm(&connection->timer);
    os_timer_arm(&connection->timer, delay, 0);
}

static void ICACHE_FLASH_ATTR timerCallback(void *data)
{
    PropellerConnection *connection = (PropellerConnection *)data;
    int finished;
    
#ifdef STATE_DEBUG
    DBG("TIMER %s", stateName(connection->state));
#endif

    switch (connection->state) {
    case stIdle:
        // shouldn't happen
        break;
    case stReset:
        GPIO_OUTPUT_SET(connection->resetPin, 1);
        armTimer(connection, RESET_DELAY_2);
        if (connection->image || connection->file) {
            connection->state = stTxHandshake;
            programmingCB = readCallback;
        }
        else {
            httpdSendResponse(connection->connData, 200, "", -1);
            connection->state = stIdle;
        }
        break;
    case stTxHandshake:
        connection->state = stRxHandshakeStart;
        ploadInitiateHandshake(connection);
        armTimer(connection, RX_HANDSHAKE_TIMEOUT);
        break;
    case stRxHandshakeStart:
    case stRxHandshake:
        httpdSendResponse(connection->connData, 400, "RX handshake timeout\r\n", -1);
        abortLoading(connection);
        break;
    case stLoadContinue:
        if (ploadLoadImageContinue(connection, ltDownloadAndRun, &finished) == 0) {
            if (finished) {
                armTimer(connection, connection->retryDelay);
                connection->state = stVerifyChecksum;
            }
            else {
                armTimer(connection, LOAD_SEGMENT_DELAY);
                connection->state = stLoadContinue;
            }
        }
        break;
    case stVerifyChecksum:
        if (connection->retriesRemaining > 0) {
            uart_tx_one_char(UART0, 0xF9);
            armTimer(connection, connection->retryDelay);
            --connection->retriesRemaining;
        }
        else {
            httpdSendResponse(connection->connData, 400, "Checksum timeout\r\n", -1);
            abortLoading(connection);
        }
        break;
    case stStartAck:
        httpdSendResponse(connection->connData, 400, "StartAck timeout\r\n", -1);
        abortLoading(connection);
        break;
    default:
        break;
    }
    
#ifdef STATE_DEBUG
    DBG(" -> %s\n", stateName(connection->state));
#endif
}

static void ICACHE_FLASH_ATTR readCallback(char *buf, short length)
{
    PropellerConnection *connection = &myConnection;
    int cnt, version, finished;
    
#ifdef STATE_DEBUG
    DBG("READ: length %d, state %s", length, stateName(connection->state));
#endif

    switch (connection->state) {
    case stIdle:
    case stReset:
    case stTxHandshake:
    case stLoadContinue:
        // just ignore data received when we're not expecting it
        break;
    case stRxHandshakeStart:    // skip junk before handshake
        while (length > 0) {
            if (*buf == 0xEE) {
                connection->state = stRxHandshake;
                break;
            }
            DBG("Ignoring %02x looking for 0xEE\n", *buf);
            --length;
            ++buf;
        }
        if (connection->state == stRxHandshakeStart || length == 0)
            break;
        // fall through
    case stRxHandshake:
    case stStartAck:
        if ((cnt = length) > connection->bytesRemaining)
            cnt = connection->bytesRemaining;
        memcpy(&connection->buffer[connection->bytesReceived], buf, cnt);
        connection->bytesReceived += cnt;
        if ((connection->bytesRemaining -= cnt) == 0) {
            switch (connection->state) {
            case stRxHandshakeStart:
            case stRxHandshake:        
                if (ploadVerifyHandshakeResponse(connection, &version) == 0) {
                    if (ploadLoadImage(connection, ltDownloadAndRun, &finished) == 0) {
                        if (finished) {
                            armTimer(connection, connection->retryDelay);
                            connection->state = stVerifyChecksum;
                        }
                        else {
                            armTimer(connection, LOAD_SEGMENT_DELAY);
                            connection->state = stLoadContinue;
                        }
                    }
                    else {
                        httpdSendResponse(connection->connData, 400, "Load image failed\r\n", -1);
                        abortLoading(connection);
                    }
                }
                else {
                    httpdSendResponse(connection->connData, 400, "RX handshake failed\r\n", -1);
                    abortLoading(connection);
                }
                break;
            case stStartAck:
                httpdSendResponse(connection->connData, 200, (char *)connection->buffer, connection->bytesReceived);
                finishLoading(connection);
                break;
            default:
                break;
            }
        }
        break;
    case stVerifyChecksum:
        if (buf[0] == 0xFE) {
            if ((connection->bytesRemaining = connection->responseSize) > 0) {
                connection->bytesReceived = 0;
                armTimer(connection, connection->responseTimeout);
                connection->state = stStartAck;
            }
            else {
                httpdSendResponse(connection->connData, 200, "", -1);
                finishLoading(connection);
            }
        }
        else {
            httpdSendResponse(connection->connData, 400, "Checksum error\r\n", -1);
            abortLoading(connection);
        }
        break;
    default:
        break;
    }
    
#ifdef STATE_DEBUG
    DBG(" -> %s\n", stateName(connection->state));
#endif
}

#ifdef USE_AT
void ICACHE_FLASH_ATTR
uart0_baud(int rate) {
  os_printf("UART %d baud\n", rate);
  uart_div_modify(UART0, UART_CLK_FREQ / rate);
}
#endif

