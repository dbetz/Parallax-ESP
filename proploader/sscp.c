#include "esp8266.h"
#include "sscp.h"
#include "httpd.h"
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

static int sscp_initialized = 0;
static uint8_t sscp_buffer[SSCP_BUFFER_MAX + 1];
static int sscp_inside;
static int sscp_length;

static Listener listeners[SSCP_LISTENER_MAX];
static Connection connections[SSCP_CONNECTION_MAX];

#ifdef DUMP
static void dump(char *tag, uint8_t *buf, int len);
#else
#define dump(tag, buf, len)
#endif

static char needPause[16] = { ':', ',' };
static int needPauseCnt = 2;
static int pauseTimeMS = 0;

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

static Listener ICACHE_FLASH_ATTR *findListener(const char *path, int type)
{
    Listener *listener;
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

static void ICACHE_FLASH_ATTR closeListener(Listener *listener)
{
    Connection *connection = listener->connections;
    while (connection) {
        // close the connection!!
        switch (listener->type) {
        case LISTENER_HTTP:
            {
                HttpdConnData *connData = (HttpdConnData *)connection->data;
                connData->cgi = NULL;
            }
            break;
        case LISTENER_WEBSOCKET:
            {
                Websock *ws = (Websock *)connection->data;
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

static Connection ICACHE_FLASH_ATTR *allocateConnection(Listener *listener)
{
    Connection *connection;
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

static void ICACHE_FLASH_ATTR removeConnection(Connection *connection)
{
    Listener *listener = connection->listener;
    if (listener) {
        Connection **pNext = &listener->connections;
        Connection *c;
        while ((c = *pNext) != NULL && c != connection)
            pNext = &c->next;
        if (c) {
            *pNext = c->next;
            c->listener = NULL;
            c->data = NULL;
        }
    }
}

int ICACHE_FLASH_ATTR cgiSSCPHandleRequest(HttpdConnData *connData)
{
    Listener *listener;
    Connection *connection;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;
    
    // find a matching listener
    if (!(listener = findListener(connData->url, LISTENER_HTTP)))
        return HTTPD_CGI_NOTFOUND;

    // find an unused connection
    if (!(connection = allocateConnection(listener))) {
        httpdStartResponse(connData, 400);
        httpdEndHeaders(connData);
os_printf("sscp: no connections available for %s request\n", connData->url);
        httpdSend(connData, "No connections available", -1);
        return HTTPD_CGI_DONE;
    }
    connection->data = connData;

os_printf("sscp: handling %s request\n", connData->url);
        
    return HTTPD_CGI_MORE;
}

void ICACHE_FLASH_ATTR sscp_init(void)
{
    int i;
    os_memset(&listeners, 0, sizeof(listeners));
    os_memset(&connections, 0, sizeof(connections));
    for (i = 0; i < SSCP_CONNECTION_MAX; ++i)
        connections[i].index = i;
    sscp_inside = 0;
    sscp_length = 0;
    sscp_initialized = 1;
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
        sscp_sendResponse(buf);
    }
    else
        sscp_sendResponse("ERROR");
}

static void setIPAddress(void *data, char *value)
{
    sscp_sendResponse("ERROR");
}

static void setBaudrate(void *data, char *value)
{
    sscp_sendResponse("OK");
    uart_drain_tx_buffer(UART0);
    uart0_baud(atoi(value));
}

static void intGetHandler(void *data)
{
    int *pValue = (int *)data;
    sscp_sendResponse("%d", *pValue);
}

static void intSetHandler(void *data, char *value)
{
    int *pValue = (int *)data;
    *pValue = atoi(value);
    sscp_sendResponse("OK");
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
    sscp_sendResponse("OK");
}

// GET,var
static void ICACHE_FLASH_ATTR do_get(int argc, char *argv[])
{
    int i;
    
    if (argc != 2) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    for (i = 0; vars[i].name != NULL; ++i) {
        if (os_strcmp(argv[1], vars[i].name) == 0) {
            (*vars[i].getHandler)(vars[i].data);
            return;
        }
    }

    sscp_sendResponse("ERROR");
}

// SET,var,value
static void ICACHE_FLASH_ATTR do_set(int argc, char *argv[])
{
    int i;
    
    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    for (i = 0; vars[i].name != NULL; ++i) {
        if (os_strcmp(argv[1], vars[i].name) == 0) {
            (*vars[i].setHandler)(vars[i].data, argv[2]);
            return;
        }
    }

    sscp_sendResponse("ERROR");
}

// LISTEN,chan
static void ICACHE_FLASH_ATTR do_listen(int argc, char *argv[])
{
    Listener *listener;
    int i;
    
    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }

    if ((i = atoi(argv[1])) < 0 || i >= SSCP_LISTENER_MAX) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (os_strlen(argv[2]) >= SSCP_PATH_MAX) {
        sscp_sendResponse("ERROR");
        return;
    }

    listener = &listeners[i];
    closeListener(listener);

    os_printf("Listening on %d for '%s'\n", i, argv[2]);
    os_strcpy(listener->path, argv[2]);
    listener->type = LISTENER_HTTP;
    
    sscp_sendResponse("OK");
}

// POLL
static void ICACHE_FLASH_ATTR do_poll(int argc, char *argv[])
{
    Connection *connection;
    HttpdConnData *connData;
    Websock *ws;
    int i;

    if (argc != 1) {
//        sscp_sendResponse("ERROR");
        sscp_sendResponse("E:0,invalid arguments");
        return;
    }

    for (i = 0, connection = connections; i < SSCP_CONNECTION_MAX; ++i, ++connection) {
        if (connection->listener) {
            if (connection->flags & CONNECTION_INIT) {
                connection->flags &= ~CONNECTION_INIT;
                switch (connection->listener->type) {
                case LISTENER_HTTP:
                    connData = (HttpdConnData *)connection->data;
                    if (connData) {
                        switch (connData->requestType) {
                        case HTTPD_METHOD_GET:
//                            sscp_sendResponse("H:%d,GET,%s", connection->index, connData->url);
                            sscp_sendResponse("G:%d,%s", connection->index, connData->url);
                            break;
                        case HTTPD_METHOD_POST:
//                            sscp_sendResponse("H:%d,POST,%s", connection->index, connData->url);
                            sscp_sendResponse("P:%d,%s", connection->index, connData->url);
                            break;
                        default:
//                            sscp_sendResponse("E:invalid request type");
                            sscp_sendResponse("E:0,invalid request type");
                            break;
                        }
                        return;
                    }
                    break;
                case LISTENER_WEBSOCKET:
                    ws = (Websock *)connection->data;
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
//    sscp_sendResponse("N:nothing");
    sscp_sendResponse("N:0,");
}

// ARG,chan
static void ICACHE_FLASH_ATTR do_arg(int argc, char *argv[])
{
    char buf[128];
    Connection *connection;
    HttpdConnData *connData;
    int i;
    
    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if ((i = atoi(argv[1])) < 0 || i >= SSCP_CONNECTION_MAX) {
        sscp_sendResponse("ERROR");
        return;
    }

    connection = &connections[i];
    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!(connData = (HttpdConnData *)connection->data) || connData->conn == NULL) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (httpdFindArg(connData->getArgs, argv[2], buf, sizeof(buf)) == -1) {
        sscp_sendResponse("ERROR");
        return;
    }

    sscp_sendResponse(buf);
}

// POSTARG,chan
static void ICACHE_FLASH_ATTR do_postarg(int argc, char *argv[])
{
    char buf[128];
    Connection *connection;
    HttpdConnData *connData;
    int i;
    
    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if ((i = atoi(argv[1])) < 0 || i >= SSCP_CONNECTION_MAX) {
        sscp_sendResponse("ERROR");
        return;
    }

    connection = &connections[i];
    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!(connData = (HttpdConnData *)connection->data) || connData->conn == NULL) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!connData->post->buff) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (httpdFindArg(connData->post->buff, argv[2], buf, sizeof(buf)) == -1) {
        sscp_sendResponse("ERROR");
        return;
    }

    sscp_sendResponse(buf);
}

#define MAX_SENDBUFF_LEN 1024

// REPLY,chan,code,payload
static void ICACHE_FLASH_ATTR do_reply(int argc, char *argv[])
{
    Connection *connection;
    HttpdConnData *connData;
    int i;

    if (argc != 4) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if ((i = atoi(argv[1])) < 0 || i >= SSCP_CONNECTION_MAX) {
        sscp_sendResponse("ERROR");
        return;
    }

    connection = &connections[i];
    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sscp_sendResponse("ERROR");
        return;
    }
        
    if (!(connData = (HttpdConnData *)connection->data) || connData->conn == NULL) {
        sscp_sendResponse("ERROR");
        return;
    }

    char sendBuff[MAX_SENDBUFF_LEN];
    httpdSetSendBuffer(connData, sendBuff, sizeof(sendBuff));
    
    char buf[20];
    int len = os_strlen(argv[3]);
    os_sprintf(buf, "%d", len);

    httpdStartResponse(connData, atoi(argv[2]));
    httpdHeader(connData, "Content-Length", buf);
    httpdEndHeaders(connData);
    httpdSend(connData, argv[3], len);
    httpdFlushSendBuffer(connData);
    
    removeConnection(connection);
    connData->cgi = NULL;

    sscp_sendResponse("OK");
}

// WSLISTEN,chan,path
static void ICACHE_FLASH_ATTR do_wslisten(int argc, char *argv[])
{
    Listener *listener;
    int i;

    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }

    if ((i = atoi(argv[1])) < 0 || i >= SSCP_LISTENER_MAX) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (os_strlen(argv[2]) >= SSCP_PATH_MAX) {
        sscp_sendResponse("ERROR");
        return;
    }

    listener = &listeners[i];
    closeListener(listener);

    os_printf("Listening on %d for '%s'\n", i, argv[2]);
    os_strcpy(listener->path, argv[2]);
    listener->type = LISTENER_WEBSOCKET;
    
    sscp_sendResponse("OK");
}

// WSREAD,chan
static void ICACHE_FLASH_ATTR do_wsread(int argc, char *argv[])
{
    Connection *connection;
    int i;

    if (argc != 2) {
        sscp_sendResponse("ERROR");
        return;
    }

    if ((i = atoi(argv[1])) < 0 || i >= SSCP_CONNECTION_MAX) {
        sscp_sendResponse("ERROR");
        return;
    }

    connection = &connections[i];

    if (!connection->listener || connection->listener->type != LISTENER_WEBSOCKET) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (!(connection->flags & CONNECTION_RXFULL)) {
        sscp_sendResponse("EMPTY");
        return;
    }

    sscp_sendResponse(connection->rxBuffer);
    connection->flags &= ~CONNECTION_RXFULL;
}

// WSWRITE,chan,payload
static void ICACHE_FLASH_ATTR do_wswrite(int argc, char *argv[])
{
    Connection *connection;
    Websock *ws;
    int i;

    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }

    if ((i = atoi(argv[1])) < 0 || i >= SSCP_CONNECTION_MAX) {
        sscp_sendResponse("ERROR");
        return;
    }

    connection = &connections[i];

    if (!connection->listener || connection->listener->type != LISTENER_WEBSOCKET) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    ws = (Websock *)connection->data;

    char sendBuff[1024];
    httpdSetSendBuffer(ws->conn, sendBuff, sizeof(sendBuff));
    cgiWebsocketSend(ws, argv[2], os_strlen(argv[2]), WEBSOCK_FLAG_NONE);

    sscp_sendResponse("OK");
}

static void ICACHE_FLASH_ATTR websocketRecvCb(Websock *ws, char *data, int len, int flags)
{
	Connection *connection = (Connection *)ws->userData;
    if (!(connection->flags & CONNECTION_RXFULL)) {
        if (len > SSCP_RX_BUFFER_MAX)
            len = SSCP_RX_BUFFER_MAX;
        strncpy(connection->rxBuffer, data, len);
        connection->rxBuffer[SSCP_RX_BUFFER_MAX - 1] = '\0';
        connection->flags |= CONNECTION_RXFULL;
    }
}

static void ICACHE_FLASH_ATTR websocketSentCb(Websock *ws)
{
}

static void ICACHE_FLASH_ATTR websocketCloseCb(Websock *ws)
{
	Connection *connection = (Connection *)ws->userData;
    connection->data = NULL;
}

void ICACHE_FLASH_ATTR sscp_websocketConnect(Websock *ws)
{
    Listener *listener;
    Connection *connection;
    
    // find a matching listener
    if (!(listener = findListener(ws->conn->url, LISTENER_WEBSOCKET))) {
        cgiWebsocketClose(ws, 0);
        return;
    }

    // find an unused connection
    if (!(connection = allocateConnection(listener))) {
        cgiWebsocketClose(ws, 0);
        return;
    }
    connection->data = ws;

    os_printf("sscp_websocketConnect: url '%s'\n", ws->conn->url);
    ws->recvCb = websocketRecvCb;
    ws->sentCb = websocketSentCb;
    ws->closeCb = websocketCloseCb;
    ws->userData = connection;
}

static struct {
    char *cmd;
    void (*handler)(int argc, char *argv[]);
} cmds[] = {
{   "",                 do_nothing          },
{   "GET",              do_get              },
{   "SET",              do_set              },
{   "LISTEN",           do_listen           },
{   "POLL",             do_poll             },
{   "ARG",              do_arg              },
{   "POSTARG",          do_postarg          },
{   "REPLY",            do_reply            },
{   "WSLISTEN",         do_wslisten         },
{   "WSREAD",           do_wsread           },
{   "WSWRITE",          do_wswrite          },
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
    sscp_sendResponse("ERROR");
}

void ICACHE_FLASH_ATTR sscp_filter(char *buf, short len, void (*outOfBand)(void *data, char *buf, short len), void *data)
{
    uint8_t *p = (uint8_t *)buf;
    uint8_t *start = p;
    
    if (!sscp_initialized)
        sscp_init();

    if (!flashConfig.enable_sscp) {
        (*outOfBand)(data, (char *)buf, len);
        return;
    }

    dump("filter", p, len);

    while (--len >= 0) {
        if (sscp_inside) {
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

