/*
    cgiprop.c - support for HTTP requests related to the Parallax Propeller

	Copyright (c) 2016 Parallax Inc.
    See the file LICENSE.txt for licensing information.
*/

#include <esp8266.h>
#include <osapi.h>
#include "config.h"
#include "cgiprop.h"
#include "proploader.h"
#include "serbridge.h"
#include "sscp.h"
#include "uart.h"
#include "roffs.h"
#include "gpio-helpers.h"

//#define STATE_DEBUG

#define FORCE_MODEM_SLEEP
#define AUTO_LOAD_CLEAR_DISPLAY
#define AUTO_LOAD_OLED_RESET_PIN 4  // MUST BE PIN 4 ! DO NOT CHANGE WITHOUT CHECKING RELEVANT CODE IN THIS FILE, ESPECIALLY INIT CODE FOR GPIO4, PIN_FUNC_SELECT
#define AUTO_LOAD_PIN       14
#define AUTO_LOAD_PIN_STATE 0

#define MAX_SENDBUFF_LEN    2600


void ICACHE_FLASH_ATTR httpdSendResponse(HttpdConnData *connData, int code, char *message, int len)
{
    char sendBuff[MAX_SENDBUFF_LEN];
    httpdSetSendBuffer(connData, sendBuff, sizeof(sendBuff));
    httpdStartResponse(connData, code);
    httpdEndHeaders(connData);
    httpdSend(connData, message, len);
    httpdFlushSendBuffer(connData);
    httpdCgiIsDone(connData);
}

static ETSTimer resetButtonTimer;
static int resetButtonState;
static int resetButtonCount;
static int resetFourPressFired;

static void wifiLoadCompletionCB(PropellerConnection *connection, LoadStatus status);
static void loadCompletionCB(PropellerConnection *connection, LoadStatus status);
static void startLoading(PropellerConnection *connection, const uint8_t *image, int imageSize);
static void finishLoading(PropellerConnection *connection, LoadStatus status);
static void abortLoading(PropellerConnection *connection, LoadStatus status);
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
    
    os_printf("Version %s\n", VERSION);
    
    memset(&myConnection, 0, sizeof(PropellerConnection));
    myConnection.state = stIdle;
    resetButtonState = 1;
    resetButtonCount = 0;
    
    myConnection.p2LoaderMode = ddoff;
    myConnection.st_load_segment_delay = P1_LOAD_SEGMENT_DELAY;
    myConnection.st_load_segment_max_size = P1_LOAD_SEGMENT_MAX_SIZE;
    myConnection.st_reset_delay_2 = P1_RESET_DELAY_2;
    
    gpio_output_set(0, 0, 0, 1 << RESET_BUTTON_PIN);
    os_timer_setfn(&resetButtonTimer, resetButtonTimerCallback, 0);
    os_timer_arm(&resetButtonTimer, RESET_BUTTON_SAMPLE_INTERVAL, 1);
    
    
    #ifdef AUTO_LOAD
    
        makeGpio(AUTO_LOAD_PIN);
        PIN_PULLUP_EN(PERIPHS_IO_MUX_MTMS_U);
        GPIO_DIS_OUTPUT(AUTO_LOAD_PIN); //gpio_output_set(0, 0, 0, 1 << AUTO_LOAD_PIN);

        #ifdef AUTO_LOAD_CLEAR_DISPLAY

            
            // Init the pin (One time)). Pullup not required here as 10K pullup on BadgeWX
            makeGpio(AUTO_LOAD_OLED_RESET_PIN);
            GPIO_DIS_OUTPUT(AUTO_LOAD_OLED_RESET_PIN); // Set IO4 to input mode

            if (IsAutoLoadEnabled()) { // DO NOT clear OLED if user has WX_BOOT asserted!
            
                os_printf("Autoload Clear Display at Startup\n");

                // Pulse IO4 to clear display
                GPIO_OUTPUT_SET(AUTO_LOAD_OLED_RESET_PIN, 0); // Set output, low
                
                //os_delay_us(100L);
                //GPIO_DIS_OUTPUT(AUTO_LOAD_OLED_RESET_PIN); // Set IO4 to input mode (release Display RESET pin))

                //os_printf("Autoload Clear Display done\n");
                
            }
            
        #endif

            
        #ifdef FORCE_MODEM_SLEEP
            
            // This could be useful in both auto-load and non-auto-load modes,
            // but keep only in "badge" auto-load mode for initial testing.
            wifi_set_sleep_type(MODEM_SLEEP_T);
            
        #endif    
            

    #endif


    os_printf("Using pin %d for reset\n", flashConfig.reset_pin);
    makeGpio(flashConfig.reset_pin);
    // GPIO_OUTPUT_SET(flashConfig.reset_pin, 1);
    GPIO_DIS_OUTPUT(flashConfig.reset_pin);

    uint32_t fs_base, fs_size;
    fs_base = roffs_base_address(&fs_size);
    
    int ret;
    if ((ret = roffs_mount(fs_base, fs_size)) != 0) {
        os_printf("Mounting flash filesystem failed: %d\n", ret);
        os_printf("Attempting to format...");
        if ((ret = roffs_format(fs_base)) != 0) {
            os_printf("Error formatting filesystem: %d\n", ret);
            return -1;
        }
        os_printf("Flash filesystem formatted.\n");
        if ((ret = roffs_mount(fs_base, fs_size)) != 0) {
            os_printf("Mounting newly formatted flash filesystem failed: %d\n", ret);
            return -1;
        }
    }
    os_printf("Flash filesystem mounted!\n");
    

    int i=0;
    char fileName[100];
    int fileSize;
    while (roffs_fileinfo(i, fileName, &fileSize) == 0) {
        os_printf("file %d: %s %d\n", i, fileName, fileSize);
        ++i;
    }


    #ifdef AUTO_LOAD

        #ifdef AUTO_LOAD_CLEAR_DISPLAY

            GPIO_DIS_OUTPUT(AUTO_LOAD_OLED_RESET_PIN); // Set IO4 to input mode (release Display RESET pin))
            os_printf("Autoload Clear Display done\n");
            
        #endif

        if (IsAutoLoadEnabledOnly()) {


            int sts;
            os_printf("Autoloading 'autorun.bin'\n");
            if ((sts = loadFile("autorun.bin")) == lsOK)
                os_printf("Autoload started\n");
            else
                os_printf("Autoload failed: %d\n", sts);

        }

    #endif

    return 0;
}

int ICACHE_FLASH_ATTR cgiPropLoad(HttpdConnData *connData) // This func is called when SimpleIDE/BlocklyProp perform overair firmware programming of Propeller 1
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

#ifdef AUTO_LOAD
    if (IsAutoLoadEnabled()) {
        httpdSendResponse(connData, 400, "Not allowed\r\n", -1);
        return HTTPD_CGI_DONE;
    }
#endif

    if (connData->post->len == 0) {
        httpdSendResponse(connData, 400, "No data\r\n", -1);
        return HTTPD_CGI_DONE;
    }
    else if (connData->post->buffLen != connData->post->len) {
        httpdSendResponse(connData, 400, "Data too large\r\n", -1);
        return HTTPD_CGI_DONE;
    }

    connData->cgiData = connection;
    connection->connData = connData;

    if (!getIntArg(connData, "baud-rate", &connection->baudRate))
        connection->baudRate = flashConfig.loader_baud_rate;
    if (!getIntArg(connData, "final-baud-rate", &connection->finalBaudRate))
        connection->finalBaudRate = flashConfig.baud_rate;
//    if (!getIntArg(connData, "reset-pin", &connection->resetPin))
        connection->resetPin = flashConfig.reset_pin;
    if (!getIntArg(connData, "response-size", &connection->responseSize))
        connection->responseSize = 0;
    if (!getIntArg(connData, "response-timeout", &connection->responseTimeout))
        connection->responseTimeout = 1000;
    
    // P1 only feature, so force timing values to P1 mode
    connection->p2LoaderMode = ddoff;
    connection->st_load_segment_delay = P1_LOAD_SEGMENT_DELAY;
    connection->st_load_segment_max_size = P1_LOAD_SEGMENT_MAX_SIZE;
    connection->st_reset_delay_2 = P1_RESET_DELAY_2;

    DBG("cgiPropLoad: size %d, baud-rate %d, final-baud-rate %d, reset-pin %d, reset-delay %d\n", connData->post->buffLen, connection->baudRate, connection->finalBaudRate, connection->resetPin, connection->st_reset_delay_2);
    
    if (connection->responseSize > 0)
        DBG("  responseSize %d, responseTimeout %d\n", connection->responseSize, connection->responseTimeout);

    connection->file = NULL;
    connection->completionCB = wifiLoadCompletionCB;
    startLoading(connection, (uint8_t *)connData->post->buff, connData->post->buffLen);

    return HTTPD_CGI_MORE;
}

int ICACHE_FLASH_ATTR cgiPropLoadP1File(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    
    connection->p2LoaderMode = ddoff;
    connection->st_load_segment_delay = P1_LOAD_SEGMENT_DELAY;
    connection->st_load_segment_max_size = P1_LOAD_SEGMENT_MAX_SIZE;
    connection->st_reset_delay_2 = P1_RESET_DELAY_2;
    
    DBG("cgiPropLoadP1File: reset-pin %d, reset-delay %d\n", connection->resetPin, connection->st_reset_delay_2);

    return cgiPropLoadFile(connData);

}

int ICACHE_FLASH_ATTR cgiPropLoadP2File(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    
    connection->p2LoaderMode = dragdrop;
    connection->st_load_segment_delay = P2_LOAD_SEGMENT_DELAY;
    connection->st_load_segment_max_size = P2_LOAD_SEGMENT_MAX_SIZE;
    connection->st_reset_delay_2 = P2_RESET_DELAY_2;
    
    DBG("cgiPropLoadP2File: reset-pin %d, reset-delay %d\n", connection->resetPin, connection->st_reset_delay_2);
    
    return cgiPropLoadFile(connData);

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

#ifdef AUTO_LOAD
    if (IsAutoLoadEnabled()) {
        httpdSendResponse(connData, 400, "Not allowed\r\n", -1);
        return HTTPD_CGI_DONE;
    }
#endif

    connData->cgiData = connection;
    connection->connData = connData;

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
        connection->baudRate = flashConfig.loader_baud_rate;
    if (!getIntArg(connData, "final-baud-rate", &connection->finalBaudRate))
        connection->finalBaudRate = flashConfig.baud_rate;
//    if (!getIntArg(connData, "reset-pin", &connection->resetPin))
    connection->resetPin = flashConfig.reset_pin;

    DBG("load-file: file %s, size %d, baud-rate %d, final-baud-rate %d, reset-pin %d, reset-delay %d\n", fileName, fileSize, connection->baudRate, connection->finalBaudRate, connection->resetPin, connection->st_reset_delay_2);
    
    connection->completionCB = wifiLoadCompletionCB;
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

#ifdef AUTO_LOAD
    if (IsAutoLoadEnabled()) {
        httpdSendResponse(connData, 400, "Not allowed\r\n", -1);
        return HTTPD_CGI_DONE;
    }
#endif

    connData->cgiData = connection;
    connection->connData = connData;

    // turn off SSCP during loading
    flashConfig.sscp_enable = 0;

    os_timer_setfn(&connection->timer, timerCallback, connection);

//    if (!getIntArg(connData, "reset-pin", &connection->resetPin))
        connection->resetPin = flashConfig.reset_pin;

    if (!getIntArg(connData, "reset-delay", &connection->st_reset_delay_2))
        connection->st_reset_delay_2 = P1_RESET_DELAY_2; // default to P1 reset delay

    DBG("reset: pin %d, delay %d\n", connection->resetPin, connection->st_reset_delay_2);
    
    connection->image = NULL;

//    makeGpio(connection->resetPin);
    GPIO_OUTPUT_SET(connection->resetPin, 0);
    connection->state = stReset;
    armTimer(connection, RESET_DELAY_1);

    return HTTPD_CGI_MORE;
}

static void ICACHE_FLASH_ATTR wifiLoadCompletionCB(PropellerConnection *connection, LoadStatus status)
{
    char *msg = NULL;
    char buf[128];

    switch (status) {
    case lsOK:
        msg = "OK\r\n";
        break;
    case lsAckResponse:
        httpdSendResponse(connection->connData, 200, (char *)connection->buffer, connection->bytesReceived);
        break;
    case lsBusy:
        os_sprintf(buf, "Transfer already in progress: state %s\r\n", stateName(connection->state));
        msg = buf;
        break;
    case lsRXHandshakeTimeout:
        msg = "RX handshake timeout\r\n";
        break;
    case lsChecksumTimeout:
        msg = "Checksum timeout\r\n";
        break;
    case lsStartAckTImeout:
        msg = "StartAck timeout\r\n";
        break;
    case lsRXHandshakeFailed:
        msg = "RX handshake failed\r\n";
        break;
    case lsWrongPropellerVersion:
        os_sprintf(buf, "Wrong Propeller version: got %d, expected 1\r\n", connection->version);
        msg = buf;
        break;
    case lsLoadImageFailed:
        msg = "Load image failed\r\n";
        break;
    case lsChecksumError:
        msg = "Checksum error\r\n";
        break;
    default:
        msg = "Internal error\r\n";
        break;
    }

    if (msg) {
        httpdSendResponse(connection->connData, status < lsFirstError ? 200 : 400, msg, -1);
    }
}

LoadStatus ICACHE_FLASH_ATTR loadBuffer(const uint8_t *image, int imageSize)
{
    PropellerConnection *connection = &myConnection;

    if (connection->state != stIdle) {
        return lsBusy;
    }

    connection->baudRate = flashConfig.loader_baud_rate;
    connection->finalBaudRate = flashConfig.baud_rate;
    connection->resetPin = flashConfig.reset_pin;
    connection->responseSize = 0;

    connection->file = NULL;
    connection->completionCB = loadCompletionCB;
    startLoading(connection, image, imageSize);

    return lsOK;
}

LoadStatus ICACHE_FLASH_ATTR loadFile(char *fileName)
{
    PropellerConnection *connection = &myConnection;
    int fileSize = 0;

    if (connection->state != stIdle) {
        return lsBusy;
    }

    if (!(connection->file = roffs_open(fileName))) {
        return lsFileNotFound;
    }
    fileSize = roffs_file_size(connection->file);

    connection->baudRate = flashConfig.loader_baud_rate;
    connection->finalBaudRate = flashConfig.baud_rate;
    connection->resetPin = flashConfig.reset_pin;
    connection->responseSize = 0;

    connection->completionCB = loadCompletionCB;
    startLoading(connection, NULL, fileSize);

    return lsOK;
}

static void ICACHE_FLASH_ATTR loadCompletionCB(PropellerConnection *connection, LoadStatus status)
{
    if (status < lsFirstError)
        os_printf("Load completed successfully\n");
    else
        os_printf("Load failed: %d\n", status);
}

static void ICACHE_FLASH_ATTR startLoading(PropellerConnection *connection, const uint8_t *image, int imageSize)
{
    connection->image = image;
    connection->imageSize = imageSize;

    // turn off SSCP during loading
    flashConfig.sscp_enable = 0;

    os_timer_setfn(&connection->timer, timerCallback, connection);

    uart0_config(connection->baudRate, ONE_STOP_BIT);

    // makeGpio(connection->resetPin);
    GPIO_OUTPUT_SET(connection->resetPin, 0);
    armTimer(connection, RESET_DELAY_1);
    connection->state = stReset;
}

static void ICACHE_FLASH_ATTR finishLoading(PropellerConnection *connection, LoadStatus status)
{
    if (connection->finalBaudRate != connection->baudRate);
        uart0_config(connection->finalBaudRate, flashConfig.stop_bits);
    if (connection->completionCB)
        (*connection->completionCB)(connection, status);
    programmingCB = NULL;
    connection->state = stIdle;
}

static void ICACHE_FLASH_ATTR abortLoading(PropellerConnection *connection, LoadStatus status)
{
    if (connection->completionCB)
        (*connection->completionCB)(connection, status);
    programmingCB = NULL;
    connection->state = stIdle;
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
//        GPIO_OUTPUT_SET(connection->resetPin, 1);
        GPIO_DIS_OUTPUT(connection->resetPin);
        armTimer(connection, connection->st_reset_delay_2);
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
        abortLoading(connection, lsRXHandshakeTimeout);
        break;
    case stLoadContinue:
        if (ploadLoadImageContinue(connection, ltDownloadAndRun, &finished) == 0) {
            if (finished) {
                armTimer(connection, connection->retryDelay);
                connection->state = stVerifyChecksum;
            }
            else {
                armTimer(connection, connection->st_load_segment_delay);
                connection->state = stLoadContinue;
            }
        }
        break;
    case stVerifyChecksum:
        if (connection->retriesRemaining > 0) {
            
            uart_tx_one_char(UART0, (connection->p2LoaderMode == dragdrop) ? 0x20 : 0xF9); // Space is ignored by P2 chip- included here for debugging clarity
                        
            armTimer(connection, connection->retryDelay);
            --connection->retriesRemaining;
        }
        else {
            abortLoading(connection, lsChecksumTimeout);
        }
        break;
    case stStartAck:
        abortLoading(connection, lsStartAckTImeout);
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
    int cnt, finished;

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
            if (*buf == (connection->p2LoaderMode == dragdrop)? 0x0d : 0xee) { // 0x0d = P2, 0xee = P1
                connection->state = stRxHandshake;
                break;
            }
            //httpd_printf("Ignoring %02x looking for %02x\n", *buf, (connection->p2LoaderMode == dragdrop)? 0x0d : 0xee);
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
                if (ploadVerifyHandshakeResponse(connection) != 0) {
                    abortLoading(connection, lsRXHandshakeFailed);
                }
                else if (connection->version != 1) {
                    abortLoading(connection, lsWrongPropellerVersion);
                }
                else {
                        if (ploadLoadImage(connection, ltDownloadAndRun, &finished) == 0) {
                            if (finished) {
                                armTimer(connection, connection->retryDelay);
                                connection->state = stVerifyChecksum;
                            }
                            else {
                                armTimer(connection, connection->st_load_segment_delay);
                                connection->state = stLoadContinue;
                            }
                        }
                        else {
                            abortLoading(connection, lsLoadImageFailed);
                        }
                }
                break;
            case stStartAck:
                finishLoading(connection, lsAckResponse);
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
                finishLoading(connection, lsOK);
            }
        }
        else {
            abortLoading(connection, lsChecksumError);
        }
        break;
    default:
        break;
    }

#ifdef STATE_DEBUG
    DBG(" -> %s\n", stateName(connection->state));
#endif
}

#ifdef AUTO_LOAD
int ICACHE_FLASH_ATTR IsAutoLoadEnabled(void) {
   
    
    // Check if Override set by API "LOCK:" command
    
    int lockstate = cmds_check_lockstate();
    
    // LOCK:x
    // x=0, neutral-default (does nothing - lock state set by auto-load pin (WX_BOOT pin), as has been the case on last firmware)
    // x=1, remove lock (overrides lock state on auto-load pin)
    // x=2, force lock (overrides lock state on auto-load pin)
    
    if (lockstate == 1) { return 0; } // No lock
    else if (lockstate == 2) { return 1; } // Lock ON
    
    // Otherwise… Lock according to WX_BOOT pin state (or pwr-button controlled lock/unlock override)

    return IsAutoLoadEnabledOnly(); 

    
}

int ICACHE_FLASH_ATTR IsAutoLoadEnabledOnly(void)
{
    int autoLoadButtonState = GPIO_INPUT_GET(AUTO_LOAD_PIN);
    static int lastAutoLoadButtonState = 0;

    static uint32_t lastAutoLoadTime = 0;
    uint32_t autoLoadTime = system_get_time() / 1000;

    if (lastAutoLoadButtonState == 1 && autoLoadTime - lastAutoLoadTime < 5000) {
       autoLoadButtonState = 1; // Override if less than 5 seconds ellapsed since last call
    }

    lastAutoLoadButtonState = autoLoadButtonState;
    lastAutoLoadTime = autoLoadTime;
    
    return ((autoLoadButtonState == AUTO_LOAD_PIN_STATE) && (GPIO_INPUT_GET(RESET_BUTTON_PIN) != 0));

    
}
#endif

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
                    
                    
                    /* Feature removed - does not fire reliabily. Timing issue probably.
                      
                      
                    #ifdef AUTO_LOAD
                    #ifdef AUTO_LOAD_CLEAR_DISPLAY

                    if (buttonPressCount > 0 && (buttonTime - lastButtonTime > RESET_BUTTON_PRESS_DELTA) && (buttonTime - lastButtonTime < RESET_BUTTON_PRESS_DELTA_MAX)) {
                        
                        os_printf("Autoload Clear Display = %d\n", buttonPressCount);
                        
                        // Pulse IO4 to clear display
                        GPIO_OUTPUT_SET(AUTO_LOAD_OLED_RESET_PIN, 0); // Set output, low
                        os_delay_us(100L);
                        GPIO_DIS_OUTPUT(AUTO_LOAD_OLED_RESET_PIN); // Set IO4 to input mode (release Display RESET pin))
                        
                        //os_printf("Autoload Clear Display done\n");
                        
                    }
                    #endif
                    #endif*/
                    
                    
                    
                    if (buttonPressCount == 0 || buttonTime - lastButtonTime > RESET_BUTTON_PRESS_DELTA) {
                        
                        buttonPressCount = 1;
                        
                    }
                                        

                    else if (++buttonPressCount == RESET_BUTTON_PRESS_COUNT) {
                        
                        // - Update 2022.Oct.24.
                        // - Only allow Reset Button recovery once after each power-cycle
                        // - Note: Could disarm the timer after first call, but for customer remote debugging add a debuglog
                        // -   message to represent if multiple attempts being made, and only disarm after the 2nd attempt.
                        
                        if (resetFourPressFired == 0) {
                            os_printf("Entering STA+AP mode\n");
                            wifi_set_opmode(STATIONAP_MODE);
                            resetFourPressFired = 1;
                            buttonPressCount = 0;
                            
                        } else {
                            os_printf("Ignore 4-press recovery request. Powercycle required.\n");
                            os_timer_disarm(&resetButtonTimer);
                            buttonPressCount = 0;
                        }
                        
                    }
                    
                    
                    /*#ifdef AUTO_LOAD
                    #ifdef AUTO_LOAD_CLEAR_DISPLAY
                    
                    else if (buttonPressCount == RESET_BUTTON_PRESS_COUNT_OLED) {
                    
                        os_printf("Autoload Clear Display at bc = %d\n", buttonPressCount);
                        
                        // Pulse IO4 to clear display
                        GPIO_OUTPUT_SET(AUTO_LOAD_OLED_RESET_PIN, 0); // Set output, low
                        os_delay_us(10L);
                        GPIO_DIS_OUTPUT(AUTO_LOAD_OLED_RESET_PIN); // Set IO4 to input mode (release Display RESET pin))
                        
                        os_printf("Autoload Clear Display done\n");
                    }
                    
                    #endif
                    #endif*/
                    
                    
                    lastButtonTime = buttonTime;
                }

            }

        }
    }
    previousState = newState;
}