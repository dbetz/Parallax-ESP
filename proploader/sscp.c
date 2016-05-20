#include "esp8266.h"
#include "sscp.h"
#include "httpd.h"
#include "uart.h"

//#define DUMP

#define SSCP_START          0xFE
#define SSCP_BUFFER_MAX     128
#define SSCP_MAX_ARGS       8
#define SSCP_LISTENER_MAX   2
#define SSCP_CONNECTION_MAX 4
#define SSCP_PATH_MAX       32
#define SSCP_RX_BUFFER_MAX  1024

#define SSCP_DEF_ENABLE     0

static int sscp_initialized = 0;
static int sscp_enabled;
static uint8_t sscp_buffer[SSCP_BUFFER_MAX + 1];
static int sscp_inside;
static int sscp_length;

typedef struct Listener Listener;
typedef struct Connection Connection;

#define LISTENER_UNUSED     0
#define LISTENER_HTTP       1
#define LISTENER_WEBSOCKET  2

struct Listener {
    int type;
    char path[SSCP_PATH_MAX];
    Connection *connections;
};

static Listener listeners[SSCP_LISTENER_MAX];

#define CONNECTION_INIT     0x0001
#define CONNECTION_RXFULL   0x0002

struct Connection {
    int index;
    int flags;
    Listener *listener;
    char rxBuffer[SSCP_RX_BUFFER_MAX];
    void *data; // HttpConnData or Websock
    Connection *next;
};

static Connection connections[SSCP_CONNECTION_MAX];

#ifdef DUMP
static void dump(char *tag, uint8_t *buf, int len);
#else
#define dump(tag, buf, len)
#endif

static void ICACHE_FLASH_ATTR sendResponse(char *fmt, ...)
{
    char buf[100];
    uart_tx_one_char(UART0, SSCP_START);
    uart_tx_one_char(UART0, '=');
    va_list ap;
    va_start(ap, fmt);
    ets_vsnprintf(buf, sizeof(buf), fmt, ap);
    os_printf("Replying '%c=%s'\n", SSCP_START, buf);
    uart0_tx_buffer(buf, os_strlen(buf));
    va_end(ap);
    uart_tx_one_char(UART0, '\r');
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
    sscp_enabled = SSCP_DEF_ENABLE;
}

void ICACHE_FLASH_ATTR sscp_enable(int enable)
{
    sscp_enabled = enable;
}

int ICACHE_FLASH_ATTR sscp_isEnabled(void)
{
    return sscp_enabled;
}

// LISTEN,chan
static void ICACHE_FLASH_ATTR do_listen(int argc, char *argv[])
{
    Listener *listener;
    int i;
    
    if (argc != 3) {
        sendResponse("ERROR");
        return;
    }

    if ((i = atoi(argv[1])) < 0 || i >= SSCP_LISTENER_MAX) {
        sendResponse("ERROR");
        return;
    }

    if (os_strlen(argv[2]) >= SSCP_PATH_MAX) {
        sendResponse("ERROR");
        return;
    }

    listener = &listeners[i];
    closeListener(listener);

    os_printf("Listening on %d for '%s'\n", i, argv[2]);
    os_strcpy(listener->path, argv[2]);
    listener->type = LISTENER_HTTP;
    
    sendResponse("OK");
}

// POLL
static void ICACHE_FLASH_ATTR do_poll(int argc, char *argv[])
{
    Connection *connection;
    HttpdConnData *connData;
    Websock *ws;
    int i;

    if (argc != 1) {
//        sendResponse("ERROR");
        sendResponse("E:0,invalid arguments");
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
//                            sendResponse("H:%d,GET,%s", connection->index, connData->url);
                            sendResponse("G:%d,%s", connection->index, connData->url);
                            break;
                        case HTTPD_METHOD_POST:
//                            sendResponse("H:%d,POST,%s", connection->index, connData->url);
                            sendResponse("P:%d,%s", connection->index, connData->url);
                            break;
                        default:
//                            sendResponse("E:invalid request type");
                            sendResponse("E:0,invalid request type");
                            break;
                        }
                        return;
                    }
                    break;
                case LISTENER_WEBSOCKET:
                    ws = (Websock *)connection->data;
                    if (ws) {
                        sendResponse("W:%d,%s", connection->index, ws->conn->url);
                        return;
                    }
                    break;
                default:
                    break;
                }
            }
            else if (connection->flags & CONNECTION_RXFULL) {
                connection->flags &= ~CONNECTION_RXFULL;
                sendResponse("D:%d,%s", connection->index, connection->rxBuffer);
                return;
            }
        }
    }
//    sendResponse("N:nothing");
    sendResponse("N:0,");
}

// ARG,chan
static void ICACHE_FLASH_ATTR do_arg(int argc, char *argv[])
{
    char buf[128];
    Connection *connection;
    HttpdConnData *connData;
    int i;
    
    if (argc != 3) {
        sendResponse("ERROR");
        return;
    }
    
    if ((i = atoi(argv[1])) < 0 || i >= SSCP_CONNECTION_MAX) {
        sendResponse("ERROR");
        return;
    }

    connection = &connections[i];
    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sendResponse("ERROR");
        return;
    }
    
    if (!(connData = (HttpdConnData *)connection->data) || connData->conn == NULL) {
        sendResponse("ERROR");
        return;
    }
    
    if (httpdFindArg(connData->getArgs, argv[2], buf, sizeof(buf)) == -1) {
        sendResponse("ERROR");
        return;
    }

    sendResponse(buf);
}

// POSTARG,chan
static void ICACHE_FLASH_ATTR do_postarg(int argc, char *argv[])
{
    char buf[128];
    Connection *connection;
    HttpdConnData *connData;
    int i;
    
    if (argc != 3) {
        sendResponse("ERROR");
        return;
    }
    
    if ((i = atoi(argv[1])) < 0 || i >= SSCP_CONNECTION_MAX) {
        sendResponse("ERROR");
        return;
    }

    connection = &connections[i];
    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sendResponse("ERROR");
        return;
    }
    
    if (!(connData = (HttpdConnData *)connection->data) || connData->conn == NULL) {
        sendResponse("ERROR");
        return;
    }
    
    if (!connData->post->buff) {
        sendResponse("ERROR");
        return;
    }
    
    if (httpdFindArg(connData->post->buff, argv[2], buf, sizeof(buf)) == -1) {
        sendResponse("ERROR");
        return;
    }

    sendResponse(buf);
}

#define MAX_SENDBUFF_LEN 1024

// REPLY,chan,code,payload
static void ICACHE_FLASH_ATTR do_reply(int argc, char *argv[])
{
    Connection *connection;
    HttpdConnData *connData;
    int i;

    if (argc != 4) {
        sendResponse("ERROR");
        return;
    }
    
    if ((i = atoi(argv[1])) < 0 || i >= SSCP_CONNECTION_MAX) {
        sendResponse("ERROR");
        return;
    }

    connection = &connections[i];
    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sendResponse("ERROR");
        return;
    }
        
    if (!(connData = (HttpdConnData *)connection->data) || connData->conn == NULL) {
        sendResponse("ERROR");
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

    sendResponse("OK");
}

// WSLISTEN,chan,path
static void ICACHE_FLASH_ATTR do_wslisten(int argc, char *argv[])
{
    Listener *listener;
    int i;

    if (argc != 3) {
        sendResponse("ERROR");
        return;
    }

    if ((i = atoi(argv[1])) < 0 || i >= SSCP_LISTENER_MAX) {
        sendResponse("ERROR");
        return;
    }

    if (os_strlen(argv[2]) >= SSCP_PATH_MAX) {
        sendResponse("ERROR");
        return;
    }

    listener = &listeners[i];
    closeListener(listener);

    os_printf("Listening on %d for '%s'\n", i, argv[2]);
    os_strcpy(listener->path, argv[2]);
    listener->type = LISTENER_WEBSOCKET;
    
    sendResponse("OK");
}

// WSREAD,chan
static void ICACHE_FLASH_ATTR do_wsread(int argc, char *argv[])
{
    Connection *connection;
    int i;

    if (argc != 2) {
        sendResponse("ERROR");
        return;
    }

    if ((i = atoi(argv[1])) < 0 || i >= SSCP_CONNECTION_MAX) {
        sendResponse("ERROR");
        return;
    }

    connection = &connections[i];

    if (!connection->listener || connection->listener->type != LISTENER_WEBSOCKET) {
        sendResponse("ERROR");
        return;
    }

    if (!(connection->flags & CONNECTION_RXFULL)) {
        sendResponse("EMPTY");
        return;
    }

    sendResponse(connection->rxBuffer);
    connection->flags &= ~CONNECTION_RXFULL;
}

// WSWRITE,chan,payload
static void ICACHE_FLASH_ATTR do_wswrite(int argc, char *argv[])
{
    Connection *connection;
    Websock *ws;
    int i;

    if (argc != 3) {
        sendResponse("ERROR");
        return;
    }

    if ((i = atoi(argv[1])) < 0 || i >= SSCP_CONNECTION_MAX) {
        sendResponse("ERROR");
        return;
    }

    connection = &connections[i];

    if (!connection->listener || connection->listener->type != LISTENER_WEBSOCKET) {
        sendResponse("ERROR");
        return;
    }
    
    ws = (Websock *)connection->data;

    char sendBuff[1024];
    httpdSetSendBuffer(ws->conn, sendBuff, sizeof(sendBuff));
    cgiWebsocketSend(ws, argv[2], os_strlen(argv[2]), WEBSOCK_FLAG_NONE);

    sendResponse("OK");
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
{   "LISTEN",   do_listen   },
{   "POLL",     do_poll     },
{   "ARG",      do_arg      },
{   "POSTARG",  do_postarg  },
{   "REPLY",    do_reply    },
{   "WSLISTEN", do_wslisten },
{   "WSREAD",   do_wsread   },
{   "WSWRITE",  do_wswrite  },
{   NULL,       NULL        }
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
        }
    }
}

void ICACHE_FLASH_ATTR sscp_filter(char *buf, short len, void (*outOfBand)(void *data, char *buf, short len), void *data)
{
    uint8_t *p = (uint8_t *)buf;
    uint8_t *start = p;
    
    if (!sscp_initialized)
        sscp_init();

    if (!sscp_enabled) {
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

