#include "esp8266.h"
#include "sscp.h"
#include "uart.h"
#include "config.h"
#include "cgiwifi.h"

//#define DUMP

#define SSCP_START          0xFE
#define SSCP_BUFFER_MAX     128
#define SSCP_MAX_ARGS       8
#define SSCP_LISTENER_MAX   2
#define SSCP_CONNECTION_MAX 4

#define SSCP_DEF_ENABLE     0

static uint8_t sscp_buffer[SSCP_BUFFER_MAX + 1];
static int sscp_inside;
static int sscp_length;

static char *sscp_payload;
static int sscp_payload_length;
static void (*sscp_payload_cb)(void *data);
static void *sscp_payload_data;

static sscp_listener listeners[SSCP_LISTENER_MAX];
static sscp_connection connections[SSCP_CONNECTION_MAX];

static char needPause[16] = { ':', ',' };
static int needPauseCnt = 2;
static int pauseTimeMS = 0;

#ifdef DUMP
static void dump(char *tag, uint8_t *buf, int len);
#else
#define dump(tag, buf, len)
#endif

void ICACHE_FLASH_ATTR sscp_init(void)
{
    int i;
    os_memset(&listeners, 0, sizeof(listeners));
    os_memset(&connections, 0, sizeof(connections));
    for (i = 0; i < SSCP_CONNECTION_MAX; ++i)
        connections[i].index = i;
    sscp_inside = 0;
    sscp_length = 0;
    sscp_payload = NULL;
    flashConfig.enable_sscp = SSCP_DEF_ENABLE;
}

void ICACHE_FLASH_ATTR sscp_enable(int enable)
{
    flashConfig.enable_sscp = enable;
}

int ICACHE_FLASH_ATTR sscp_isEnabled(void)
{
    return flashConfig.enable_sscp;
}

void ICACHE_FLASH_ATTR sscp_capturePayload(char *buf, int length, void (*cb)(void *data), void *data)
{
    sscp_payload = buf;
    sscp_payload_length = length;
    sscp_payload_cb = cb;
    sscp_payload_data = data;
}

sscp_listener ICACHE_FLASH_ATTR *sscp_get_listener(int i)
{
    return i >= 0 && i < SSCP_LISTENER_MAX ? &listeners[i] : NULL;
}

sscp_listener ICACHE_FLASH_ATTR *sscp_find_listener(const char *path, int type)
{
    sscp_listener *listener;
    int i;

    // find a matching listener
    for (i = 0, listener = listeners; i < SSCP_LISTENER_MAX; ++i, ++listener) {

        // only check channels to which the MCU is listening
        if (listener->type == type) {
os_printf("listener: matching '%s' with '%s'\n", listener->path, path);

            // check for a literal match
            if (os_strcmp(listener->path, path) == 0)
                return listener;
            
            // check for a wildcard match
            else {
                int len_m1 = os_strlen(listener->path) - 1;
                if (listeners->path[len_m1] == '*' && os_strncmp(listeners->path, path, len_m1) == 0)
                    return listener;
            }
        }
    }

    // not found
    return NULL;
}

void ICACHE_FLASH_ATTR sscp_close_listener(sscp_listener *listener)
{
    sscp_connection *connection = listener->connections;
    while (connection) {
        // close the connection!!
        switch (listener->type) {
        case LISTENER_HTTP:
            {
                HttpdConnData *connData = connection->d.http.conn;
                connData->cgi = NULL;
            }
            break;
        case LISTENER_WEBSOCKET:
            {
                Websock *ws = connection->d.ws.ws;
                cgiWebsocketClose(ws, 0);
            }
            break;
        default:
            break;
        }
        connection->listener = NULL;
        connection = connection->next;
    }
    listener->connections = NULL;
}

sscp_connection ICACHE_FLASH_ATTR *sscp_get_connection(int i)
{
    return i >= 0 && i < SSCP_CONNECTION_MAX ? &connections[i] : NULL;
}

sscp_connection ICACHE_FLASH_ATTR *sscp_allocate_connection(sscp_listener *listener)
{
    sscp_connection *connection;
    int i;
    for (i = 0, connection = connections; i < SSCP_CONNECTION_MAX; ++i, ++connection) {
        if (!connection->listener) {
            connection->flags = CONNECTION_INIT;
            connection->listener = listener;
            connection->next = listener->connections;
            listener->connections = connection;
            return connection;
        }
    }
    return NULL;
}

void ICACHE_FLASH_ATTR sscp_free_connection(sscp_connection *connection)
{
    connection->listener = NULL;
}

void ICACHE_FLASH_ATTR sscp_remove_connection(sscp_connection *connection)
{
    sscp_listener *listener = connection->listener;
    if (listener) {
        sscp_connection **pNext = &listener->connections;
        sscp_connection *c;
        while ((c = *pNext) != NULL) {
            if (c == connection) {
                *pNext = c->next;
                sscp_free_connection(c);
                break;
            }
            pNext = &c->next;
        }
    }
}

void ICACHE_FLASH_ATTR sscp_sendResponse(char *fmt, ...)
{
    char buf[128];
    int cnt;

    // insert the header
    buf[0] = SSCP_START;
    buf[1] = '=';

    // insert the formatted response
    va_list ap;
    va_start(ap, fmt);
    cnt = ets_vsnprintf(&buf[2], sizeof(buf) - 3, fmt, ap);
    va_end(ap);

    // check to see if the response was truncated
    if (cnt >= sizeof(buf) - 3)
        cnt = sizeof(buf) - 3 - 1;

    // display the response before inserting the final \r
    os_printf("Replying: '%s'\n", &buf[1]);

    // terminate the response with a \r
    buf[2 + cnt] = '\r';
    cnt += 3;

    if (pauseTimeMS > 0) {
        char *p = buf;
        while (--cnt >= 0) {
            int byte = *p++;
            int i;
            uart_tx_one_char(UART0, byte);
            for (i = 0; i < needPauseCnt; ++i) {
                if (byte == needPause[i]) {
//os_printf("Delaying after '%c' 0x%02x for %d MS\n", byte, byte, pauseTimeMS);
                    uart_drain_tx_buffer(UART0);
                    os_delay_us(pauseTimeMS * 1000);
                }
            }
        }
    }
    else {
        uart_tx_buffer(UART0, buf, cnt);
    }
}

static void getIPAddress(void *data)
{
    struct ip_info info;
	char buf[128];
    if (wifi_get_ip_info(STATION_IF, &info)) {
	    os_sprintf(buf, "%d.%d.%d.%d", 
		    (info.ip.addr >> 0) & 0xff,
            (info.ip.addr >> 8) & 0xff, 
		    (info.ip.addr >>16) & 0xff,
            (info.ip.addr >>24) & 0xff);
        sscp_sendResponse("S,%s", buf);
    }
    else
        sscp_sendResponse("N,0");
}

static void setIPAddress(void *data, char *value)
{
    sscp_sendResponse("E,%d", SSCP_ERROR_UNIMPLEMENTED);
}

static void setBaudrate(void *data, char *value)
{
    sscp_sendResponse("S,0");
    uart_drain_tx_buffer(UART0);
    uart0_baud(atoi(value));
}

static void intGetHandler(void *data)
{
    int *pValue = (int *)data;
    sscp_sendResponse("S,%d", *pValue);
}

static void intSetHandler(void *data, char *value)
{
    int *pValue = (int *)data;
    *pValue = atoi(value);
    sscp_sendResponse("S,0");
}

static struct {
    char *name;
    void (*getHandler)(void *data);
    void (*setHandler)(void *data, char *value);
    void *data;
} vars[] = {
{   "ip-address",       getIPAddress,   setIPAddress,   NULL                        },
{   "pause-time",       intGetHandler,  intSetHandler,  &pauseTimeMS                },
{   "enable-sscp",      intGetHandler,  intSetHandler,  &flashConfig.enable_sscp    },
{   "baud-rate",        intGetHandler,  setBaudrate,    &uart0_baudRate             },
{   NULL,               NULL,           NULL,           NULL                        }
};

// (nothing)
static void ICACHE_FLASH_ATTR do_nothing(int argc, char *argv[])
{
    sscp_sendResponse("S,0");
}

// GET,var
static void ICACHE_FLASH_ATTR do_get(int argc, char *argv[])
{
    int i;
    
    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    for (i = 0; vars[i].name != NULL; ++i) {
        if (os_strcmp(argv[1], vars[i].name) == 0) {
            (*vars[i].getHandler)(vars[i].data);
            return;
        }
    }

    sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
}

// SET,var,value
static void ICACHE_FLASH_ATTR do_set(int argc, char *argv[])
{
    int i;
    
    if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    for (i = 0; vars[i].name != NULL; ++i) {
        if (os_strcmp(argv[1], vars[i].name) == 0) {
            (*vars[i].setHandler)(vars[i].data, argv[2]);
            return;
        }
    }

    sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
}

// POLL
static void ICACHE_FLASH_ATTR do_poll(int argc, char *argv[])
{
    sscp_connection *connection;
    HttpdConnData *connData;
    Websock *ws;
    int i;

    if (argc != 1) {
        sscp_sendResponse("E:%,invalid arguments", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }

    for (i = 0, connection = connections; i < SSCP_CONNECTION_MAX; ++i, ++connection) {
        if (connection->listener) {
            if (connection->flags & CONNECTION_INIT) {
                connection->flags &= ~CONNECTION_INIT;
                switch (connection->listener->type) {
                case LISTENER_HTTP:
                    connData = (HttpdConnData *)connection->d.http.conn;
                    if (connData) {
                        switch (connData->requestType) {
                        case HTTPD_METHOD_GET:
                            sscp_sendResponse("G:%d,%s", connection->index, connData->url);
                            break;
                        case HTTPD_METHOD_POST:
                            sscp_sendResponse("P:%d,%s", connection->index, connData->url);
                            break;
                        default:
                            sscp_sendResponse("E:0,invalid request type");
                            break;
                        }
                        return;
                    }
                    break;
                case LISTENER_WEBSOCKET:
                    ws = (Websock *)connection->d.ws.ws;
                    if (ws) {
                        sscp_sendResponse("W:%d,%s", connection->index, ws->conn->url);
                        return;
                    }
                    break;
                default:
                    break;
                }
            }
            else if (connection->flags & CONNECTION_RXFULL) {
                connection->flags &= ~CONNECTION_RXFULL;
                sscp_sendResponse("D:%d,%s", connection->index, connection->rxBuffer);
                return;
            }
        }
    }
    
    sscp_sendResponse("N:0,");
}

static struct {
    char *cmd;
    void (*handler)(int argc, char *argv[]);
} cmds[] = {
{   "",                 do_nothing          },
{   "GET",              do_get              },
{   "SET",              do_set              },
{   "POLL",             do_poll             },
{   "LISTEN",           http_do_listen      },
{   "ARG",              http_do_arg         },
{   "POSTARG",          http_do_postarg     },
{   "REPLY",            http_do_reply       },
{   "WSLISTEN",         ws_do_wslisten      },
{   "WSREAD",           ws_do_wsread        },
{   "WSWRITE",          ws_do_wswrite       },
{   "TCP-CONNECT",      tcp_do_connect      },
{   "TCP-DISCONNECT",   tcp_do_disconnect   },
{   "TCP-SEND",         tcp_do_send         },
{   "TCP-RECV",         tcp_do_recv         },
{   NULL,               NULL                }
};

static void ICACHE_FLASH_ATTR sscp_process(char *buf, short len)
{
    char *argv[SSCP_MAX_ARGS + 1];
    char *p, *next;
    int argc, i;
    
    dump("sscp", (uint8_t *)buf, len);
    
    p = buf;
    argc = 0;
    while ((next = os_strchr(p, ',')) != NULL) {
        if (argc < SSCP_MAX_ARGS)
            argv[argc++] = p;
        *next++ = '\0';
        p = next;
    }
    if (argc < SSCP_MAX_ARGS)
        argv[argc++] = p;
    argv[argc] = NULL;
        
#ifdef DUMP
    for (i = 0; i < argc; ++i)
        os_printf("argv[%d] = '%s'\n", i, argv[i]);
#endif

    for (i = 0; cmds[i].cmd; ++i) {
        if (strcmp(argv[0], cmds[i].cmd) == 0) {
            os_printf("Calling '%s' handler\n", argv[0]);
            (*cmds[i].handler)(argc, argv);
            return;
        }
    }

    os_printf("No handler for '%s'\n", argv[0]);
    sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_REQUEST);
}

void ICACHE_FLASH_ATTR sscp_filter(char *buf, short len, void (*outOfBand)(void *data, char *buf, short len), void *data)
{
    uint8_t *p = (uint8_t *)buf;
    uint8_t *start = p;
    
    if (!flashConfig.enable_sscp) {
        (*outOfBand)(data, (char *)buf, len);
        return;
    }

    dump("filter", p, len);

    while (--len >= 0) {
        if (sscp_payload) {
            *sscp_payload++ = *p++;
            if (--sscp_payload_length == 0) {
                sscp_payload = NULL;
                (*sscp_payload_cb)(sscp_payload_data);
            }
            ++start;
        }
        else if (sscp_inside) {
            if (*p == '\n') {
                sscp_buffer[sscp_length] = '\0';
                sscp_process((char *)sscp_buffer, sscp_length);
                sscp_inside = 0;
                start = ++p;
            }
            else if (*p == '\r')
                ++p;
            else if (sscp_length < SSCP_BUFFER_MAX)
                sscp_buffer[sscp_length++] = *p++;
            else {
                // sscp command too long
                sscp_inside = 0;
                start = p++;
            }
        }
        else {
            if (*p == SSCP_START) {
                if (p > start && outOfBand) {
                    dump("outOfBand", start, p - start);
                    (*outOfBand)(data, (char *)start, p - start);
                }
                sscp_inside = 1;
                sscp_length = 0;
                ++p;
            }
            else {
                // just accumulate data outside of a command
                ++p;
            }
        }
    }
    if (!sscp_inside && p > start && outOfBand) {
        dump("outOfBand", start, p - start);
        (*outOfBand)(data, (char *)start, p - start);
    }
}

#ifdef DUMP
static void ICACHE_FLASH_ATTR dump(char *tag, uint8_t *buf, int len)
{
    int i = 0;
    os_printf("%s[%d]: '", tag, len);
    while (i < len)
        os_printf("%c", buf[i++]);
    os_printf("'\n");
}
#endif

