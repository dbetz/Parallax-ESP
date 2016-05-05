#include <stdarg.h>
#include "esp8266.h"
#include "sscp.h"
#include "httpd.h"
#include "uart.h"

//#define DUMP

#define SSCP_START          '$'
#define SSCP_BUFFER_MAX     128
#define SSCP_MAX_ARGS       8
#define SSCP_PATH_MAX       32

#define SSCP_HTTP_MAX       4

#define SSCP_WS_MAX         4
#define SSCP_WS_BUFFER_MAX  128

static int sscp_initialized = 0;
static char sscp_buffer[SSCP_BUFFER_MAX + 1];
static int sscp_inside;
static int sscp_length;

#define HTTP_LISTEN 0x0001

typedef struct {
    int flags;
    char path[SSCP_PATH_MAX];
    HttpdConnData *connData;
} Handler;

// only support one path for the moment
static Handler handlers[SSCP_HTTP_MAX];

#define WS_LISTEN   0x0001
#define WS_FULL     0x0002

typedef struct {
    int flags;
    char path[SSCP_PATH_MAX];
    char buffer[SSCP_WS_BUFFER_MAX];
    Websock *ws;
} WSHandler;

// only support one path for the moment
static WSHandler wsHandlers[SSCP_WS_MAX];

#ifdef DUMP
static void dump(char *tag, char *buf, int len);
#else
#define dump(tag, buf, len)
#endif

static void ICACHE_FLASH_ATTR sendResponse(char *fmt, ...)
{
    char buf[100];
    uart0_write_char(SSCP_START);
    uart0_write_char('=');
    va_list ap;
    va_start(ap, fmt);
    ets_vsnprintf(buf, sizeof(buf), fmt, ap);
    os_printf("Replying '%c=%s'\n", SSCP_START, buf);
    uart0_tx_buffer(buf, os_strlen(buf));
    va_end(ap);
    uart0_write_char('\r');
}

int ICACHE_FLASH_ATTR cgiSSCPHandleRequest(HttpdConnData *connData)
{
    int match = 0;
    Handler *h;
    int chan;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;
    
    // find a matching handler
    for (chan = 0, h = handlers; chan < SSCP_HTTP_MAX; ++chan, ++h) {

        // only check channels to which the MCU is listening
        if (h->flags & HTTP_LISTEN) {
os_printf("sscp: matching '%s' with '%s'\n", h->path, connData->url);

            // check for a literal match
            if (os_strcmp(h->path, connData->url) == 0) {
                match = 1;
                break;
            }
            
            // check for a wildcard match
            else {
                int len_m1 = os_strlen(h->path) - 1;
                if (h->path[len_m1] == '*' && os_strncmp(h->path, connData->url, len_m1) == 0) {
                    match = 1;
                    break;
                }
            }
        }
    }
    
    // check if we can handle this request
    if (!match)
        return HTTPD_CGI_NOTFOUND;
os_printf("sscp: handling request on channel %d\n", chan);
        
    // store the connection data for sending a response
    h->connData = connData;
    
    os_printf("SSCP_Request: '%s'\n", h->path);
    
    return HTTPD_CGI_MORE;
}

void ICACHE_FLASH_ATTR sscp_init(void)
{
    os_memset(&handlers, 0, sizeof(handlers));
    os_memset(&wsHandlers, 0, sizeof(wsHandlers));
    sscp_inside = 0;
    sscp_length = 0;
    sscp_initialized = 1;
}

// LISTEN,chan,path
static void do_listen(int argc, char *argv[])
{
    Handler *h;
    int chan;
    
    if (argc != 3 || os_strlen(argv[2]) >= SSCP_PATH_MAX) {
        sendResponse("ERROR");
        return;
    }

    if ((chan = atoi(argv[1])) < 0 || chan >= SSCP_HTTP_MAX) {
        sendResponse("ERROR");
        return;
    }

    h = &handlers[chan];
    
    os_printf("Listening on %d for '%s'\n", chan, argv[2]);
    os_strcpy(h->path, argv[2]);
    h->flags |= HTTP_LISTEN;
    
    sendResponse("OK");
}

// POLL,chan
static void do_poll(int argc, char *argv[])
{
    char *requestType = "";
    char *url = "";
    Handler *h;
    int chan;
    
    if (argc != 2) {
        sendResponse("ERROR");
        return;
    }
    
    if ((chan = atoi(argv[1])) < 0 || chan >= SSCP_HTTP_MAX) {
        sendResponse("ERROR");
        return;
    }

    h = &handlers[chan];
    
    if (h->connData) {
        switch (h->connData->requestType) {
        case HTTPD_METHOD_GET:
            requestType = "GET";
            break;
        case HTTPD_METHOD_POST:
            requestType = "POST";
            break;
        default:
            requestType = "ERROR";
            break;
        }
        url = h->connData->url;
    }
    
    sendResponse("%s,%s", requestType, url);
}

// ARG,chan
static void do_arg(int argc, char *argv[])
{
    char buf[128];
    Handler *h;
    int chan;
    
    if (argc != 3) {
        sendResponse("ERROR");
        return;
    }
    
    if ((chan = atoi(argv[1])) < 0 || chan >= SSCP_HTTP_MAX) {
        sendResponse("ERROR");
        return;
    }

    h = &handlers[chan];
    
    if (!h->connData || h->connData->conn == NULL) {
        h->connData = NULL;
        sendResponse("ERROR");
        return;
    }
    
    if (httpdFindArg(h->connData->getArgs, argv[2], buf, sizeof(buf)) == -1) {
        sendResponse("ERROR");
        return;
    }

    sendResponse(buf);
}

// POSTARG,chan
static void do_postarg(int argc, char *argv[])
{
    char buf[128];
    Handler *h;
    int chan;
    
    if (argc != 3) {
        sendResponse("ERROR");
        return;
    }
    
    if ((chan = atoi(argv[1])) < 0 || chan >= SSCP_HTTP_MAX) {
        sendResponse("ERROR");
        return;
    }

    h = &handlers[chan];
    
    if (!h->connData || h->connData->conn == NULL) {
        h->connData = NULL;
        sendResponse("ERROR");
        return;
    }
    
    if (!h->connData->post->buff) {
        sendResponse("ERROR");
        return;
    }
    
    if (httpdFindArg(h->connData->post->buff, argv[2], buf, sizeof(buf)) == -1) {
        sendResponse("ERROR");
        return;
    }

    sendResponse(buf);
}

#define MAX_SENDBUFF_LEN 1024

// REPLY,code,payload
static void do_reply(int argc, char *argv[])
{
    Handler *h;
    int chan;

    if (argc != 4) {
        sendResponse("ERROR");
        return;
    }
    
    if ((chan = atoi(argv[1])) < 0 || chan >= SSCP_HTTP_MAX) {
        sendResponse("ERROR");
        return;
    }

    h = &handlers[chan];
    
    if (!h->connData || h->connData->conn == NULL) {
        h->connData = NULL;
        sendResponse("ERROR");
        return;
    }
    
    char sendBuff[MAX_SENDBUFF_LEN];
    httpdSetSendBuffer(h->connData, sendBuff, sizeof(sendBuff));
    
    char buf[20];
    int len = os_strlen(argv[3]);
    os_sprintf(buf, "%d", len);

    httpdStartResponse(h->connData, atoi(argv[2]));
    httpdHeader(h->connData, "Content-Length", buf);
    httpdEndHeaders(h->connData);
    httpdSend(h->connData, argv[3], len);
    httpdFlushSendBuffer(h->connData);
    
    h->connData->cgi = NULL;
    h->connData = NULL;
    
    sendResponse("OK");
}

// WSLISTEN,chan,path
static void do_wslisten(int argc, char *argv[])
{
}

// WSREAD,chan
static void do_wsread(int argc, char *argv[])
{
}

// WSWRITE,chan,payload
static void do_wswrite(int argc, char *argv[])
{
    WSHandler *h;
    int chan;

    if ((chan = atoi(argv[1])) < 0 || chan >= SSCP_WS_MAX) {
        sendResponse("ERROR");
        return;
    }

    h = &wsHandlers[chan];
    
    if (!h->ws || !h->ws->conn || h->ws->conn->conn == NULL) {
        h->ws = NULL;
        sendResponse("ERROR");
        return;
    }
    
    if (argc != 3) {
        sendResponse("ERROR");
        return;
    }

    cgiWebsocketSend(h->ws, argv[2], os_strlen(argv[2]), WEBSOCK_FLAG_NONE);
}

static void websocketRecv(Websock *ws, char *data, int len, int flags)
{
	int i;
	char buff[128];
	os_sprintf(buff, "You sent SSCP: ");
	for (i=0; i<len; i++) buff[i+10]=data[i];
	buff[i+10]=0;
	cgiWebsocketSend(ws, buff, os_strlen(buff), WEBSOCK_FLAG_NONE);
}

void sscp_websocketConnect(Websock *ws)
{
	ws->recvCb = websocketRecv;
    os_printf("sscp_websocketConnect: url '%s'\n", ws->conn->url);
	cgiWebsocketSend(ws, "Hi, SSCP Websocket!", 14, WEBSOCK_FLAG_NONE);
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

void ICACHE_FLASH_ATTR sscp_process(char *buf, short len)
{
    char *argv[SSCP_MAX_ARGS + 1];
    char *p, *next;
    int argc, i;
    
    dump("sscp", buf, len);
    
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
    char *start = buf;
    
    if (!sscp_initialized)
        sscp_init();

    dump("filter", buf, len);

    while (--len >= 0) {
        if (sscp_inside) {
            if (*buf == '\n') {
                sscp_buffer[sscp_length] = '\0';
                sscp_process(sscp_buffer, sscp_length);
                sscp_inside = 0;
                start = ++buf;
            }
            else if (*buf == '\r')
                ++buf;
            else if (sscp_length < SSCP_BUFFER_MAX)
                sscp_buffer[sscp_length++] = *buf++;
            else {
                // sscp command too long
                sscp_inside = 0;
                start = buf++;
            }
        }
        else {
            if (*buf == SSCP_START) {
                if (buf > start && outOfBand) {
                    dump("outOfBand", start, buf - start);
                    (*outOfBand)(data, start, buf - start);
                }
                sscp_inside = 1;
                sscp_length = 0;
                ++buf;
            }
            else {
                // just accumulate data outside of a command
                ++buf;
            }
        }
    }
    if (buf > start && outOfBand) {
        dump("outOfBand", start, buf - start);
        (*outOfBand)(data, start, buf - start);
    }
}

#ifdef DUMP
static void ICACHE_FLASH_ATTR dump(char *tag, char *buf, int len)
{
    int i = 0;
    os_printf("%s[%d]: '", tag, len);
    while (i < len)
        os_printf("%c", buf[i++]);
    os_printf("'\n");
}
#endif

