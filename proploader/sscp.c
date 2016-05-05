#include <stdarg.h>
#include "esp8266.h"
#include "sscp.h"
#include "httpd.h"
#include "uart.h"

//#define DUMP

#define SSCP_START      '$'
#define SSCP_MAX        128
#define SSCP_MAX_ARGS   8
#define SSCP_PATH_MAX   32

static int sscp_initialized = 0;
static char sscp_buffer[SSCP_MAX + 1];
static int sscp_inside;
static int sscp_length;

typedef struct {
    char path[SSCP_PATH_MAX];
    HttpdConnData *connData;
} Handler;

// only support one path for the moment
static Handler handler;

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
    Handler *h = &handler; // only one for now!
    int match = 0;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;
        
os_printf("sscp: matching '%s' with '%s'\n", h->path, connData->url);
    // check for a literal match
    if (os_strcmp(h->path, connData->url) == 0)
        match = 1;
        
    // check for a wildcard match
    else {
        int len_m1 = os_strlen(h->path) - 1;
        if (h->path[len_m1] == '*' && os_strncmp(h->path, connData->url, len_m1) == 0)
            match = 1;
    }
    
    // check if we can handle this request
    if (!match)
        return HTTPD_CGI_NOTFOUND;
os_printf("sscp: handling request\n");
        
    // store the connection data for sending a response
    h->connData = connData;
    
    os_printf("SSCP_Request: '%s'\n", h->path);
    
    return HTTPD_CGI_MORE;
}

void ICACHE_FLASH_ATTR sscp_init(void)
{
    handler.connData = NULL;
    sscp_inside = 0;
    sscp_length = 0;
    sscp_initialized = 1;
}

static void do_listen(int argc, char *argv[])
{
    Handler *h = &handler; // only one for now!
    
    if (argc != 2 || os_strlen(argv[1]) >= SSCP_PATH_MAX) {
        sendResponse("ERROR");
        return;
    }
    
    os_printf("Listening for '%s'\n", argv[1]);
    os_strcpy(h->path, argv[1]);
    
    sendResponse("OK");
}

static void do_poll(int argc, char *argv[])
{
    Handler *h = &handler; // only one for now!
    char *requestType = "";
    char *url = "";
    
    if (argc != 1) {
        sendResponse("ERROR");
        return;
    }
    
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

//int httpdFindArg(char *line, char *arg, char *buff, int buffLen);

static void do_arg(int argc, char *argv[])
{
    Handler *h = &handler; // only one for now!
    char buf[128];
    
    if (!h->connData || h->connData->conn == NULL) {
        h->connData = NULL;
        sendResponse("ERROR");
        return;
    }
    
    if (argc != 2) {
        sendResponse("ERROR");
        return;
    }
    
    if (httpdFindArg(h->connData->getArgs, argv[1], buf, sizeof(buf)) == -1) {
        sendResponse("ERROR");
        return;
    }

    sendResponse(buf);
}

static void do_postarg(int argc, char *argv[])
{
    Handler *h = &handler; // only one for now!
    char buf[128];
    
    if (!h->connData || h->connData->conn == NULL) {
        h->connData = NULL;
        sendResponse("ERROR");
        return;
    }
    
    if (argc != 2) {
        sendResponse("ERROR");
        return;
    }
    
    if (!h->connData->post->buff) {
        sendResponse("ERROR");
        return;
    }
    
    if (httpdFindArg(h->connData->post->buff, argv[1], buf, sizeof(buf)) == -1) {
        sendResponse("ERROR");
        return;
    }

    sendResponse(buf);
}

#define MAX_SENDBUFF_LEN 1024

static void do_reply(int argc, char *argv[])
{
    Handler *h = &handler; // only one for now!

    if (!h->connData || h->connData->conn == NULL) {
        h->connData = NULL;
        sendResponse("ERROR");
        return;
    }
    
    if (argc != 3) {
        sendResponse("ERROR");
        return;
    }
    
    char sendBuff[MAX_SENDBUFF_LEN];
    httpdSetSendBuffer(h->connData, sendBuff, sizeof(sendBuff));
    
    char buf[20];
    int len = os_strlen(argv[2]);
    os_sprintf(buf, "%d", len);

    httpdStartResponse(h->connData, atoi(argv[1]));
    httpdHeader(h->connData, "Content-Length", buf);
    httpdEndHeaders(h->connData);
    httpdSend(h->connData, argv[2], len);
    httpdFlushSendBuffer(h->connData);
    
    h->connData->cgi = NULL;
    h->connData = NULL;
    
    sendResponse("OK");
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
            else if (sscp_length < SSCP_MAX)
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

