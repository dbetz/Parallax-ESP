#include "esp8266.h"
#include "sscp.h"
#include "uart.h"
#include "config.h"
#include "cgiprop.h"
#include "cgiwifi.h"
#include "gpio-helpers.h"

static int getVersion(void *data, char *value)
{
    os_strcpy(value, VERSION);
    return 0;
}

static int getModuleName(void *data, char *value)
{
    os_strcpy(value, flashConfig.module_name);
    return 0;
}

static int setModuleName(void *data, char *value)
{
    os_memcpy(flashConfig.module_name, value, sizeof(flashConfig.module_name));
    flashConfig.module_name[sizeof(flashConfig.module_name) - 1] = '\0';
    softap_set_ssid(flashConfig.module_name, os_strlen(flashConfig.module_name));
    wifi_station_set_hostname(flashConfig.module_name);
    return 0;
}

static int getWiFiMode(void *data, char *value)
{
    switch (wifi_get_opmode()) {
    case STATION_MODE:
        os_strcpy(value, "STA");
        break;
    case SOFTAP_MODE:
        os_strcpy(value, "AP");
        break;
    case STATIONAP_MODE:
        os_strcpy(value, "STA+AP");
        break;
    default:
        return -1;
    }
    return 0;
}

static int setWiFiMode(void *data, char *value)
{
    int mode;
    
    if (os_strcmp(value, "STA") == 0)
        mode = STATION_MODE;
    else if (os_strcmp(value, "AP") == 0)
        mode = SOFTAP_MODE;
    else if (os_strcmp(value, "STA+AP") == 0)
        mode = STATIONAP_MODE;
    else if (isdigit((int)value[0]))
        mode = atoi(value);
    else
        return -1;
        
    switch (mode) {
    case STATION_MODE:
        os_printf("Entering STA mode\n");
        break;
    case SOFTAP_MODE:
        os_printf("Entering AP mode\n");
        break;
    case STATIONAP_MODE:
        os_printf("Entering STA+AP mode\n");
        break;
    default:
        os_printf("Unknown wi-fi mode: %d\n", mode);
        return -1;
    }

    if (mode != wifi_get_opmode())
        wifi_set_opmode(mode);
    
    return 0;
}

static int getWiFiSSID(void *data, char *value)
{
	struct station_config config;
	if (!wifi_station_get_config(&config))
	    return -1;
	strcpy(value, (char *)config.ssid);
	return 0;
}

static int getIPAddress(void *data, char *value)
{
    int interface = (int)data;
    struct ip_info info;
    
    if (!wifi_get_ip_info(interface, &info))
        return -1;
        
    os_sprintf(value, "%d.%d.%d.%d", 
        (info.ip.addr >> 0) & 0xff,
        (info.ip.addr >> 8) & 0xff, 
        (info.ip.addr >>16) & 0xff,
        (info.ip.addr >>24) & 0xff);
        
    return 0;
}

static int setIPAddress(void *data, char *value)
{
    return -1;
}

static int getMACAddress(void *data, char *value)
{
    int interface = (int)data;
    uint8 addr[6];
    
    if (!wifi_get_macaddr(interface, addr))
        return -1;
        
    os_sprintf(value, "%02x:%02x:%02x:%02x:%02x:%02x", 
        addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
        
    return 0;
}

static int setMACAddress(void *data, char *value)
{
    return -1;
}

static int setBaudrate(void *data, char *value)
{
    flashConfig.baud_rate = atoi(value);
    uart_drain_tx_buffer(UART0);
    uart0_config(flashConfig.baud_rate, flashConfig.stop_bits);
    return 0;
}

static int setStopBits(void *data, char *value)
{
    flashConfig.stop_bits = atoi(value);
    uart_drain_tx_buffer(UART0);
    uart0_config(flashConfig.baud_rate, flashConfig.stop_bits);
    return 0;
}

static int setDbgBaudrate(void *data, char *value)
{
    flashConfig.dbg_baud_rate = atoi(value);
    uart_drain_tx_buffer(UART1);
    uart1_config(flashConfig.dbg_baud_rate, flashConfig.dbg_stop_bits);
    return 0;
}

static int setDbgStopBits(void *data, char *value)
{
    flashConfig.dbg_stop_bits = atoi(value);
    uart_drain_tx_buffer(UART1);
    uart1_config(flashConfig.dbg_baud_rate, flashConfig.dbg_stop_bits);
    return 0;
}

static int setResetPin(void *data, char *value)
{
    flashConfig.reset_pin = atoi(value);
    makeGpio(flashConfig.reset_pin);
    GPIO_OUTPUT_SET(flashConfig.reset_pin, 1);
    return 0;
}

static int setLoaderBaudrate(void *data, char *value)
{
    flashConfig.loader_baud_rate = atoi(value);
    return 0;
}

static int getPauseChars(void *data, char *value)
{
    strncpy(value, flashConfig.sscp_need_pause, flashConfig.sscp_need_pause_cnt);
    value[flashConfig.sscp_need_pause_cnt] = '\0';
    return 0;
}

static int setPauseChars(void *data, char *value)
{
    if (os_strlen(value) >= sizeof(flashConfig.sscp_need_pause))
        return -1;
    os_strcpy(flashConfig.sscp_need_pause, value);
    flashConfig.sscp_need_pause_cnt = os_strlen(value);
    return 0;
}

// the following four functions are from Espressif sample code

void ICACHE_FLASH_ATTR gpio16_output_conf(void)
{
    WRITE_PERI_REG(PAD_XPD_DCDC_CONF,
                   (READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xffffffbc) | (uint32)0x1);  // mux configuration for XPD_DCDC to output rtc_gpio0
    WRITE_PERI_REG(RTC_GPIO_CONF,
                   (READ_PERI_REG(RTC_GPIO_CONF) & (uint32)0xfffffffe) | (uint32)0x0);  //mux configuration for out enable
    WRITE_PERI_REG(RTC_GPIO_ENABLE,
                   (READ_PERI_REG(RTC_GPIO_ENABLE) & (uint32)0xfffffffe) | (uint32)0x1);    //out enable
}

void ICACHE_FLASH_ATTR gpio16_output_set(uint8 value)
{
    WRITE_PERI_REG(RTC_GPIO_OUT,
                   (READ_PERI_REG(RTC_GPIO_OUT) & (uint32)0xfffffffe) | (uint32)(value & 1));
}

void ICACHE_FLASH_ATTR gpio16_input_conf(void)
{
    WRITE_PERI_REG(PAD_XPD_DCDC_CONF,
                   (READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xffffffbc) | (uint32)0x1);  // mux configuration for XPD_DCDC and rtc_gpio0 connection
    WRITE_PERI_REG(RTC_GPIO_CONF,
                   (READ_PERI_REG(RTC_GPIO_CONF) & (uint32)0xfffffffe) | (uint32)0x0);  //mux configuration for out enable
    WRITE_PERI_REG(RTC_GPIO_ENABLE,
                   READ_PERI_REG(RTC_GPIO_ENABLE) & (uint32)0xfffffffe);    //out disable
}

uint8 ICACHE_FLASH_ATTR gpio16_input_get(void)
{
    return (uint8)(READ_PERI_REG(RTC_GPIO_IN_DATA) & 1);
}

enum {
    PIN_GPIO0 = 0,      // PGM
    PIN_GPIO1 = 1,      // UART RX
    PIN_GPIO2 = 2,      // DBG
    PIN_GPIO3 = 3,      // UART TX
    PIN_GPIO4 = 4,      // SEL
    PIN_GPIO5 = 5,      // ASC
    PIN_GPIO12 = 12,    // DTR
    PIN_GPIO13 = 13,    // CTS
    PIN_GPIO14 = 14,    // 
    PIN_GPIO15 = 15,    // RTS
    PIN_GPIO16 = 16,
};

static int getPinHandler(void *data, char *value)
{
    int pin = (int)data;
    int ivalue = 0;
    switch (pin) {
    case PIN_GPIO0:
    case PIN_GPIO1:
    case PIN_GPIO2:
    case PIN_GPIO3:
    case PIN_GPIO4:
    case PIN_GPIO5:
    case PIN_GPIO12:
    case PIN_GPIO13:
    case PIN_GPIO14:
    case PIN_GPIO15:
        makeGpio(pin);
        GPIO_DIS_OUTPUT(pin);
        ivalue = GPIO_INPUT_GET(pin);
        break;
    case PIN_GPIO16:
        gpio16_input_conf();
        ivalue = gpio_input_get();
        break;
    default:
        return -1;
    }
    os_sprintf(value, "%d", ivalue);
    return 0;
}

static int setPinHandler(void *data, char *value)
{
    int pin = (int)data;
    switch (pin) {
    case PIN_GPIO0:
    case PIN_GPIO1:
    case PIN_GPIO2:
    case PIN_GPIO3:
    case PIN_GPIO4:
    case PIN_GPIO5:
    case PIN_GPIO12:
    case PIN_GPIO13:
    case PIN_GPIO14:
    case PIN_GPIO15:
        makeGpio(pin);
        GPIO_OUTPUT_SET(pin, atoi(value));
        break;
    case PIN_GPIO16:
        gpio16_output_conf();
        gpio16_output_set(atoi(value));
        break;
    default:
        return -1;
    }
    return 0;
}

static int intGetHandler(void *data, char *value)
{
    int *pValue = (int *)data;
    os_sprintf(value, "%d", *pValue);
    return 0;
}

static int intSetHandler(void *data, char *value)
{
    int *pValue = (int *)data;
    *pValue = atoi(value);
    return 0;
}

static int int8GetHandler(void *data, char *value)
{
    int8_t *pValue = (int8_t *)data;
    os_sprintf(value, "%d", *pValue);
    return 0;
}

static int int8SetHandler(void *data, char *value)
{
    int8_t *pValue = (int8_t *)data;
    *pValue = atoi(value);
    return 0;
}

static int uint8SetHandler(void *data, char *value)
{
    uint8_t *pValue = (uint8_t *)data;
    *pValue = atoi(value);
    return 0;
}

static int uint8GetHandler(void *data, char *value)
{
    uint8_t *pValue = (uint8_t *)data;
    os_sprintf(value, "%d", *pValue);
    return 0;
}

typedef struct {
    char *name;
    int (*getHandler)(void *data, char *value);
    int (*setHandler)(void *data, char *value);
    void *data;
} cmd_def;

static cmd_def vars[] = {
{   "version",          getVersion,         NULL,               NULL                            },
{   "module-name",      getModuleName,      setModuleName,      NULL                            },
{   "wifi-mode",        getWiFiMode,        setWiFiMode,        NULL                            },
{   "wifi-ssid",        getWiFiSSID,        NULL,               NULL                            },
{   "station-ipaddr",   getIPAddress,       setIPAddress,       (void *)STATION_IF              },
{   "station-macaddr",  getMACAddress,      setMACAddress,      (void *)STATION_IF              },
{   "softap-ipaddr",    getIPAddress,       setIPAddress,       (void *)SOFTAP_IF               },
{   "softap-macaddr",   getMACAddress,      setMACAddress,      (void *)SOFTAP_IF               },
{   "cmd-start-char",   uint8GetHandler,    uint8SetHandler,    &flashConfig.sscp_start         },
{   "cmd-pause-time",   intGetHandler,      intSetHandler,      &flashConfig.sscp_pause_time_ms },
{   "cmd-pause-chars",  getPauseChars,      setPauseChars,      NULL                            },
{   "cmd-events",       int8GetHandler,     int8SetHandler,     &flashConfig.sscp_events        },
{   "cmd-enable",       int8GetHandler,     int8SetHandler,     &flashConfig.sscp_enable        },
{   "loader-baud-rate", intGetHandler,      setLoaderBaudrate,  &flashConfig.loader_baud_rate   },
{   "baud-rate",        intGetHandler,      setBaudrate,        &flashConfig.baud_rate          },
{   "stop-bits",        int8GetHandler,     setStopBits,        &flashConfig.stop_bits          },
{   "dbg-baud-rate",    intGetHandler,      setDbgBaudrate,     &flashConfig.dbg_baud_rate      },
{   "dbg-stop-bits",    int8GetHandler,     setDbgStopBits,     &flashConfig.dbg_stop_bits      },
{   "dbg-enable",       int8GetHandler,     int8SetHandler,     &flashConfig.dbg_enable         },
{   "reset-pin",        int8GetHandler,     setResetPin,        &flashConfig.reset_pin          },
{   "connect-led-pin",  int8GetHandler,     int8SetHandler,     &flashConfig.conn_led_pin       },
{   "rx-pullup",        int8GetHandler,     int8SetHandler,     &flashConfig.rx_pullup          },
{   "pin-gpio0",        getPinHandler,      setPinHandler,      (void *)PIN_GPIO0               },
{   "pin-gpio1",        getPinHandler,      setPinHandler,      (void *)PIN_GPIO1               },
{   "pin-gpio2",        getPinHandler,      setPinHandler,      (void *)PIN_GPIO2               },
{   "pin-gpio3",        getPinHandler,      setPinHandler,      (void *)PIN_GPIO3               },
{   "pin-gpio4",        getPinHandler,      setPinHandler,      (void *)PIN_GPIO4               },
{   "pin-gpio5",        getPinHandler,      setPinHandler,      (void *)PIN_GPIO5               },
{   "pin-gpio12",       getPinHandler,      setPinHandler,      (void *)PIN_GPIO12              },
{   "pin-gpio13",       getPinHandler,      setPinHandler,      (void *)PIN_GPIO13              },
{   "pin-gpio14",       getPinHandler,      setPinHandler,      (void *)PIN_GPIO14              },
{   "pin-gpio15",       getPinHandler,      setPinHandler,      (void *)PIN_GPIO15              },
{   NULL,               NULL,               NULL,               NULL                            }
};

// GET,var
void ICACHE_FLASH_ATTR cmds_do_get(int argc, char *argv[])
{
    char value[128];
    int i;
    
    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    for (i = 0; vars[i].name != NULL; ++i) {
        if (os_strcmp(argv[1], vars[i].name) == 0) {
            int (*handler)(void *, char *) = vars[i].getHandler;
            if (handler) {
                if ((*handler)(vars[i].data, value) == 0)
                    sscp_sendResponse("S,%s", value);
                else
                    sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
            }
            else
                sscp_sendResponse("E,%d", SSCP_ERROR_UNIMPLEMENTED);
            return;
        }
    }

    sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
}

// SET,var,value
void ICACHE_FLASH_ATTR cmds_do_set(int argc, char *argv[])
{
    int i;
    
    if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    for (i = 0; vars[i].name != NULL; ++i) {
        if (os_strcmp(argv[1], vars[i].name) == 0) {
            int (*handler)(void *, char *) = vars[i].setHandler;
            if (handler) {
                if ((*handler)(vars[i].data, argv[2]) == 0)
                    sscp_sendResponse("S,0");
                else
                    sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
            }
            else
                sscp_sendResponse("E,%d", SSCP_ERROR_UNIMPLEMENTED);
            return;
        }
    }

    sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
}

int ICACHE_FLASH_ATTR cgiPropSetting(HttpdConnData *connData)
{
    char name[128], value[128];
    cmd_def *def = NULL;
    int i;
    
#ifdef WIFI_BADGE
    if (connData->requestType == HTTPD_METHOD_POST && IsAutoLoadEnabled()) {
        httpdSendResponse(connData, 400, "Not allowed\r\n", -1);
        return HTTPD_CGI_DONE;
    }
#endif
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;

    if (httpdFindArg(connData->getArgs, "name", name, sizeof(name)) < 0) {
        httpdSendResponse(connData, 400, "Missing name argument\r\n", -1);
        return HTTPD_CGI_DONE;
    }

    for (i = 0; vars[i].name != NULL; ++i) {
        if (os_strcmp(name, vars[i].name) == 0) {
            def = &vars[i];
            break;
        }
    }
    
    if (!def) {
        httpdSendResponse(connData, 400, "Unknown setting\r\n", -1);
        return HTTPD_CGI_DONE;
    }

    // check for GET
    if (connData->requestType == HTTPD_METHOD_GET) {
        os_printf("GET '%s'", def->name);
        if ((*def->getHandler)(def->data, value) != 0) {
            os_printf(" --> ERROR\n");
            httpdSendResponse(connData, 400, "Get setting failed\r\n", -1);
            return HTTPD_CGI_DONE;
        }
        os_printf(" --> '%s'\n", value);
    }

    // only other option is POST
    else {
        if (httpdFindArg(connData->getArgs, "value", value, sizeof(value)) < 0) {
            httpdSendResponse(connData, 400, "Missing value argument\r\n", -1);
            return HTTPD_CGI_DONE;
        }
        os_printf("SET '%s' to '%s'", def->name, value);
        if ((*def->setHandler)(def->data, value) != 0) {
            os_printf(" --> ERROR\n");
            httpdSendResponse(connData, 400, "Set setting failed\r\n", -1);
            return HTTPD_CGI_DONE;
        }
        os_printf(" --> OK\n");
        os_strcpy(value, "");
    }

    int length = strlen(value);
    char buf[32];
    os_sprintf(buf, "%d", length);
    httpdStartResponse(connData, 200);
    httpdHeader(connData, "Content-Type", "text/plain");
    httpdHeader(connData, "Content-Length", buf);
    httpdEndHeaders(connData);
    httpdSend(connData, value, -1);

    return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR cgiPropSaveSettings(HttpdConnData *connData)
{
#ifdef WIFI_BADGE
    if (IsAutoLoadEnabled()) {
        httpdSendResponse(connData, 400, "Not allowed\r\n", -1);
        return HTTPD_CGI_DONE;
    }
#endif
    httpdStartResponse(connData, configSave() ? 200 : 400);
    httpdEndHeaders(connData);
    httpdSend(connData, "", -1);
    return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR cgiPropRestoreSettings(HttpdConnData *connData)
{
#ifdef WIFI_BADGE
    if (IsAutoLoadEnabled()) {
        httpdSendResponse(connData, 400, "Not allowed\r\n", -1);
        return HTTPD_CGI_DONE;
    }
#endif
    httpdStartResponse(connData, configRestore() ? 200 : 400);
    httpdEndHeaders(connData);
    httpdSend(connData, "", -1);
    return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR cgiPropRestoreDefaultSettings(HttpdConnData *connData)
{
#ifdef WIFI_BADGE
    if (IsAutoLoadEnabled()) {
        httpdSendResponse(connData, 400, "Not allowed\r\n", -1);
        return HTTPD_CGI_DONE;
    }
#endif
     httpdStartResponse(connData, configRestoreDefaults() ? 200 : 400);
    httpdStartResponse(connData, 200);
    httpdEndHeaders(connData);
    httpdSend(connData, "", -1);
    return HTTPD_CGI_DONE;
}

