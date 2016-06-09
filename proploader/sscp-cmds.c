#include "esp8266.h"
#include "sscp.h"
#include "uart.h"
#include "config.h"
#include "cgiprop.h"
#include "cgiwifi.h"

// (nothing)
void ICACHE_FLASH_ATTR cmds_do_nothing(int argc, char *argv[])
{
    sscp_sendResponse("S,0");
}

// JOIN,ssid,passwd
void ICACHE_FLASH_ATTR cmds_do_join(int argc, char *argv[])
{
    if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (wifiJoin(argv[1], argv[2]) == 0)
        sscp_sendResponse("S,0");
    else
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
}

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
    return 0;
}

static int getWiFiMode(void *data, char *value)
{
    switch (wifi_get_opmode()) {
    case STATION_MODE:
        os_strcpy(value, "Station");
        break;
    case SOFTAP_MODE:
        os_strcpy(value, "SoftAP");
        break;
    case STATIONAP_MODE:
        os_strcpy(value, "Station+SoftAP");
        break;
    default:
        return -1;
    }
    return 0;
}

static int setWiFiMode(void *data, char *value)
{
    int mode;
    
    if (os_strcmp(value, "Station") == 0)
        mode = STATION_MODE;
    else if (os_strcmp(value, "SoftAP") == 0)
        mode = SOFTAP_MODE;
    else if (os_strcmp(value, "Station+SoftAP") == 0)
        mode = STATIONAP_MODE;
    else if (isdigit((int)value[0]))
        mode = atoi(value);
    else
        return -1;
        
    if (mode != wifi_get_opmode()) {
        wifi_set_opmode(mode);
        system_restart();
    }
    
    return 0;
}

static int getWiFiSSID(void *data, char *value)
{
	struct station_config config;
	wifi_station_get_config(&config);
	strcpy(value, (char *)config.ssid);
	return 0;
}

static int getWiFiAPWarning(void *data, char *value)
{
    int mode = wifi_get_opmode();
    if (mode == SOFTAP_MODE)
        os_strcpy(value, "<b>Can't scan in this mode.</b> Click <a href=\"setmode.cgi?mode=3\">here</a> to go to STA+AP mode.");
    else
        strcpy(value, "Click <a href=\"setmode.cgi?mode=2\">here</a> to go to standalone AP mode.");
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

static int setSSCPEnable(void *data, char *value)
{
    sscp_enable(atoi(value));
    return 0;
}

enum {
    PIN_GPIO0 = 0,      // PGM
    PIN_GPIO2 = 2,      // DBG
    PIN_GPIO4 = 4,      // SEL
    PIN_GPIO5 = 5,      // ASC
    PIN_GPIO12 = 12,    // DTR
    PIN_GPIO13 = 13,    // CTS
    PIN_GPIO14 = 14,    // DIO9
    PIN_GPIO15 = 15,    // RTS
    PIN_RST,
    PIN_RES
};

static int getPinHandler(void *data, char *value)
{
    int pin = (int)data;
    int ivalue = 0;
    switch (pin) {
    case PIN_GPIO0:
    case PIN_GPIO2:
    case PIN_GPIO4:
    case PIN_GPIO5:
    case PIN_GPIO12:
    case PIN_GPIO13:
    case PIN_GPIO14:
    case PIN_GPIO15:
        GPIO_DIS_OUTPUT(pin);
        ivalue = GPIO_INPUT_GET(pin);
        break;
    case PIN_RST:
        break;
    case PIN_RES:
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
    case PIN_GPIO2:
    case PIN_GPIO4:
    case PIN_GPIO5:
    case PIN_GPIO12:
    case PIN_GPIO13:
    case PIN_GPIO14:
    case PIN_GPIO15:
        GPIO_OUTPUT_SET(pin, atoi(value));
        break;
    case PIN_RST:
        break;
    case PIN_RES:
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
{   "wifi-ap-warning",  getWiFiAPWarning,   NULL,               NULL                            },
{   "station-ipaddr",   getIPAddress,       setIPAddress,       (void *)STATION_IF              },
{   "station-macaddr",  getMACAddress,      setMACAddress,      (void *)STATION_IF              },
{   "softap-ipaddr",    getIPAddress,       setIPAddress,       (void *)SOFTAP_IF               },
{   "softap-macaddr",   getMACAddress,      setMACAddress,      (void *)SOFTAP_IF               },
{   "sscp-start-char",  intGetHandler,      intSetHandler,      &sscp_start                     },
{   "sscp-pause-time",  intGetHandler,      intSetHandler,      &flashConfig.sscp_pause_time_ms },
{   "sscp-pause-chars", getPauseChars,      setPauseChars,      NULL                            },
{   "sscp-enable",      int8GetHandler,     setSSCPEnable,      &flashConfig.sscp_enable        },
{   "loader-baud-rate", intGetHandler,      setLoaderBaudrate,  &flashConfig.loader_baud_rate   },
{   "baud-rate",        intGetHandler,      setBaudrate,        &flashConfig.baud_rate          },
{   "stop-bits",        int8GetHandler,     setStopBits,        &flashConfig.stop_bits          },
{   "dbg-baud-rate",    intGetHandler,      setDbgBaudrate,     &flashConfig.dbg_baud_rate      },
{   "dbg-stop-bits",    int8GetHandler,     setDbgStopBits,     &flashConfig.dbg_stop_bits      },
{   "reset-pin",        int8GetHandler,     int8SetHandler,     &flashConfig.reset_pin          },
{   "connect-led-pin",  int8GetHandler,     int8SetHandler,     &flashConfig.conn_led_pin       },
{   "rx-pullup",        int8GetHandler,     int8SetHandler,     &flashConfig.rx_pullup          },
{   "pin-pgm",          getPinHandler,      setPinHandler,      (void *)PIN_GPIO0               },
{   "pin-gpio0",        getPinHandler,      setPinHandler,      (void *)PIN_GPIO0               },
{   "pin-dbg",          getPinHandler,      setPinHandler,      (void *)PIN_GPIO2               },
{   "pin-gpio2",        getPinHandler,      setPinHandler,      (void *)PIN_GPIO2               },
{   "pin-sel",          getPinHandler,      setPinHandler,      (void *)PIN_GPIO4               },
{   "pin-gpio4",        getPinHandler,      setPinHandler,      (void *)PIN_GPIO4               },
{   "pin-asc",          getPinHandler,      setPinHandler,      (void *)PIN_GPIO5               },
{   "pin-gpio5",        getPinHandler,      setPinHandler,      (void *)PIN_GPIO5               },
{   "pin-dtr",          getPinHandler,      setPinHandler,      (void *)PIN_GPIO12              },
{   "pin-gpio12",       getPinHandler,      setPinHandler,      (void *)PIN_GPIO12              },
{   "pin-cts",          getPinHandler,      setPinHandler,      (void *)PIN_GPIO13              },
{   "pin-gpio13",       getPinHandler,      setPinHandler,      (void *)PIN_GPIO13              },
{   "pin-dio9",         getPinHandler,      setPinHandler,      (void *)PIN_GPIO14              },
{   "pin-gpio14",       getPinHandler,      setPinHandler,      (void *)PIN_GPIO14              },
{   "pin-rts",          getPinHandler,      setPinHandler,      (void *)PIN_GPIO15              },
{   "pin-gpio15",       getPinHandler,      setPinHandler,      (void *)PIN_GPIO15              },
{   "pin-rst",          getPinHandler,      setPinHandler,      (void *)PIN_RST                 },
{   "pin-res",          getPinHandler,      setPinHandler,      (void *)PIN_RES                 },
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

// POLL
void ICACHE_FLASH_ATTR cmds_do_poll(int argc, char *argv[])
{
    sscp_connection *connection;
    int i;

    if (argc != 1) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }

    for (i = 0, connection = sscp_connections; i < SSCP_CONNECTION_MAX; ++i, ++connection) {
        if (connection->listener) {
            if (connection->flags & CONNECTION_INIT) {
                connection->flags &= ~CONNECTION_INIT;
                switch (connection->listener->type) {
                case LISTENER_HTTP:
                    {
                        HttpdConnData *connData = (HttpdConnData *)connection->d.http.conn;
                        if (connData) {
                            switch (connData->requestType) {
                            case HTTPD_METHOD_GET:
                                sscp_sendResponse("G,%d,%d", connection->index, 0);
                                break;
                            case HTTPD_METHOD_POST:
                                sscp_sendResponse("P,%d,%d", connection->index, connData->post->buff ? connData->post->len : 0);
                                break;
                            default:
                                sscp_sendResponse("E,%d,0", SSCP_ERROR_INTERNAL_ERROR);
                                break;
                            }
                            return;
                        }
                    }
                    break;
                case LISTENER_WEBSOCKET:
                    {
                        Websock *ws = (Websock *)connection->d.ws.ws;
                        if (ws) {
                            sscp_sendResponse("W,%d,0", connection->index);
                            return;
                        }
                    }
                    break;
                default:
                    break;
                }
            }
            else if (connection->flags & CONNECTION_RXFULL) {
                sscp_sendResponse("D,%d,%d", connection->index, connection->rxCount);
                return;
            }
        }
    }
    
    sscp_sendResponse("N,0,0");
}

// PATH,chan
void ICACHE_FLASH_ATTR cmds_do_path(int argc, char *argv[])
{
    sscp_connection *connection;

    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }

    if (!connection->listener) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
    
    switch (connection->listener->type) {
    case LISTENER_HTTP:
        {
            HttpdConnData *connData = (HttpdConnData *)connection->d.http.conn;
            if (!connData || !connData->conn) {
                sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
                return;
            }
            sscp_sendResponse("S,%s", connData->url);
        }
        break;
    case LISTENER_WEBSOCKET:
        {
            Websock *ws = (Websock *)connection->d.ws.ws;
            if (!ws || !ws->conn) {
                sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
                return;
            }
            sscp_sendResponse("S,%s", ws->conn->url);
        }
        break;
    default:
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
}

// SEND,chan,payload
void ICACHE_FLASH_ATTR cmds_do_send(int argc, char *argv[])
{
    sscp_connection *c;

    if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (!(c = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
    
    if (!c->listener) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
    
    if (c->flags & CONNECTION_TXFULL) {
        sscp_sendResponse("E,%d", SSCP_ERROR_BUSY);
        return;
    }
    
    switch (c->listener->type) {
    case LISTENER_WEBSOCKET:
        ws_send_helper(c, argc, argv);
        break;
    case LISTENER_TCP:
        tcp_send_helper(c, argc, argv);
        break;
    default:
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        break;
    }
}

// RECV,chan
void ICACHE_FLASH_ATTR cmds_do_recv(int argc, char *argv[])
{
    sscp_connection *connection;

    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }

    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }

    if (!connection->listener) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }

    if (!(connection->flags & CONNECTION_RXFULL)) {
        sscp_sendResponse("N,0");
        return;
    }

    sscp_sendResponse("S,%d", connection->rxCount);
    if (connection->rxCount > 0)
        sscp_sendPayload(connection->rxBuffer, connection->rxCount);
    connection->flags &= ~CONNECTION_RXFULL;
}

int ICACHE_FLASH_ATTR cgiPropSetting(HttpdConnData *connData)
{
    char name[128], value[128];
    cmd_def *def = NULL;
    int i;
    
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
        if ((*def->getHandler)(def->data, value) != 0) {
            httpdSendResponse(connData, 400, "Get setting failed\r\n", -1);
            return HTTPD_CGI_DONE;
        }
    }

    // only other option is POST
    else {
        if (httpdFindArg(connData->getArgs, "value", value, sizeof(value)) < 0) {
            httpdSendResponse(connData, 400, "Missing value argument\r\n", -1);
            return HTTPD_CGI_DONE;
        }
        if ((*def->setHandler)(def->data, value) != 0) {
            httpdSendResponse(connData, 400, "Set setting failed\r\n", -1);
            return HTTPD_CGI_DONE;
        }
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

int ICACHE_FLASH_ATTR cgiPropModuleInfo(HttpdConnData *connData)
{
    struct ip_info sta_info;
    struct ip_info softap_info;
    uint8 sta_addr[6];
    uint8 softap_addr[6];
    char buf[1024];

    if (!wifi_get_ip_info(STATION_IF, &sta_info))
        os_memset(&sta_info, 0, sizeof(sta_info));
    if (!wifi_get_macaddr(STATION_IF, sta_addr))
        os_memset(&sta_addr, 0, sizeof(sta_addr));

    if (!wifi_get_ip_info(SOFTAP_IF, &softap_info))
        os_memset(&softap_info, 0, sizeof(softap_info));
    if (!wifi_get_macaddr(SOFTAP_IF, softap_addr))
        os_memset(&softap_addr, 0, sizeof(softap_addr));

    os_sprintf(buf, "\
{\n\
  \"name\": \"%s\",\n\
  \"sta-ipaddr\": \"%d.%d.%d.%d\",\n\
  \"sta-macaddr\": \"%02x:%02x:%02x:%02x:%02x:%02x\",\n\
  \"softap-ipaddr\": \"%d.%d.%d.%d\",\n\
  \"softap-macaddr\": \"%02x:%02x:%02x:%02x:%02x:%02x\"\n\
}\n",
        flashConfig.module_name,
        (sta_info.ip.addr >> 0) & 0xff,
        (sta_info.ip.addr >> 8) & 0xff, 
        (sta_info.ip.addr >>16) & 0xff,
        (sta_info.ip.addr >>24) & 0xff,
        sta_addr[0], sta_addr[1], sta_addr[2], sta_addr[3], sta_addr[4], sta_addr[5],
        (softap_info.ip.addr >> 0) & 0xff,
        (softap_info.ip.addr >> 8) & 0xff, 
        (softap_info.ip.addr >>16) & 0xff,
        (softap_info.ip.addr >>24) & 0xff,
        softap_addr[0], softap_addr[1], softap_addr[2], softap_addr[3], softap_addr[4], softap_addr[5]);
        
    httpdStartResponse(connData, 200);
    httpdEndHeaders(connData);
    httpdSend(connData, buf, -1);
    return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR cgiPropSaveSettings(HttpdConnData *connData)
{
    httpdStartResponse(connData, configSave() ? 200 : 400);
    httpdEndHeaders(connData);
    httpdSend(connData, "", -1);
    return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR cgiPropRestoreSettings(HttpdConnData *connData)
{
    httpdStartResponse(connData, configRestore() ? 200 : 400);
    httpdEndHeaders(connData);
    httpdSend(connData, "", -1);
    return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR cgiPropRestoreDefaultSettings(HttpdConnData *connData)
{
    httpdStartResponse(connData, configRestoreDefaults() ? 200 : 400);
    httpdStartResponse(connData, 200);
    httpdEndHeaders(connData);
    httpdSend(connData, "", -1);
    return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR tplSettings(HttpdConnData *connData, char *token, void **arg)
{
	char value[128];
	int i;
	
	if (token == NULL) return HTTPD_CGI_DONE;

    for (i = 0; vars[i].name != NULL; ++i) {
        if (os_strcmp(token, vars[i].name) == 0) {
            int (*handler)(void *, char *) = vars[i].getHandler;
            if (handler) {
                if ((*handler)(vars[i].data, value) != 0)
                    os_strcpy(value, "(error)");
            }
            else
                os_strcpy(value, "(unimplemented)");
            break;
        }
    }
    
    if (!vars[i].name)
        os_strcpy(value, "(unknown)");

	httpdSend(connData, value, -1);
	return HTTPD_CGI_DONE;
}
