#include "esp8266.h"
#include "sscp.h"
#include "uart.h"
#include "config.h"
#include "cgiwifi.h"

//#define DUMP_CMDS
//#define DUMP_ARGS
//#define DUMP_FILTER
//#define DUMP_OUTOFBAND

#define SSCP_BUFFER_MAX     128
#define SSCP_MAX_ARGS       8

#define SSCP_DEF_ENABLE     0

static uint8_t sscp_buffer[SSCP_BUFFER_MAX + 1];
static int sscp_inside;
static int sscp_length;

int sscp_start = SSCP_TKN_START;
static int sscp_processing;
static char *sscp_payload;
static int sscp_payload_length;
static int sscp_payload_remaining;
static void (*sscp_payload_cb)(void *data, int count);
static void *sscp_payload_data;

sscp_listener sscp_listeners[SSCP_LISTENER_MAX];
sscp_connection sscp_connections[SSCP_CONNECTION_MAX];

#if defined(DUMP_CMDS) || defined(DUMP_FILTER) || defined(DUMP_OUTOFBAND)
#define DUMP
#endif

#ifdef DUMP
static void dump(char *tag, uint8_t *buf, int len);
#else
#define dump(tag, buf, len)
#endif

void ICACHE_FLASH_ATTR sscp_init(void)
{
    int i;
    
    os_memset(&sscp_listeners, 0, sizeof(sscp_listeners));
    for (i = 0; i < SSCP_LISTENER_MAX; ++i)
        sscp_listeners[i].hdr.index = i;
    
    os_memset(&sscp_connections, 0, sizeof(sscp_connections));
    for (i = 0; i < SSCP_CONNECTION_MAX; ++i)
        sscp_connections[i].hdr.index = SSCP_LISTENER_MAX + i;
    
    sscp_start = SSCP_TKN_START;
    sscp_processing = 0;
    sscp_inside = 0;
    sscp_length = 0;
    sscp_payload = NULL;
    sscp_payload_length = 0;
    sscp_payload_remaining = 0;
}

void ICACHE_FLASH_ATTR sscp_reset(void)
{
    int i;
    
    for (i = 0; i < SSCP_LISTENER_MAX; ++i)
        sscp_close_listener(&sscp_listeners[i]);
    for (i = 0; i < SSCP_CONNECTION_MAX; ++i)
        sscp_close_connection(&sscp_connections[i]);
        
    sscp_start = SSCP_TKN_START;
    sscp_inside = 0;
    sscp_length = 0;
    sscp_payload = NULL;
    sscp_payload_length = 0;
    sscp_payload_remaining = 0;
}

void ICACHE_FLASH_ATTR sscp_enable(int enable)
{
    if (enable != flashConfig.sscp_enable) {
        if (enable)
            sscp_reset();
        flashConfig.sscp_enable = enable;
    }
}

int ICACHE_FLASH_ATTR sscp_isEnabled(void)
{
    return flashConfig.sscp_enable;
}

void ICACHE_FLASH_ATTR sscp_capturePayload(char *buf, int length, void (*cb)(void *data, int count), void *data)
{
    sscp_payload = buf;
    sscp_payload_length = length;
    sscp_payload_remaining = length;
    sscp_payload_cb = cb;
    sscp_payload_data = data;
}

sscp_listener ICACHE_FLASH_ATTR *sscp_get_listener(int i)
{
    if (i < 0 || i >= SSCP_LISTENER_MAX)
        return NULL;
    return &sscp_listeners[i];
}

sscp_listener *sscp_allocate_listener(int type, char *path, sscp_dispatch *dispatch)
{
    int i;
    for (i = 0; i < SSCP_LISTENER_MAX; ++i) {
        sscp_listener *listener = &sscp_listeners[i];
        if (listener->hdr.type == TYPE_UNUSED) {
            listener->hdr.type = type;
            listener->hdr.dispatch = dispatch;
            os_strcpy(listener->path, path);
            return listener;
        }
    }
    return NULL;
}

sscp_listener ICACHE_FLASH_ATTR *sscp_find_listener(const char *path, int type)
{
    sscp_listener *listener;
    int i;

    // find a matching listener
    for (i = 0, listener = sscp_listeners; i < SSCP_LISTENER_MAX; ++i, ++listener) {

        // only check channels to which the MCU is listening
        if (listener->hdr.type == type) {
os_printf("listener: matching '%s' with '%s'\n", listener->path, path);

            // check for a literal match
            if (os_strcmp(listener->path, path) == 0)
                return listener;
            
            // check for a wildcard match
            else {
                int len_m1 = os_strlen(listener->path) - 1;
                if (listener->path[len_m1] == '*' && os_strncmp(listener->path, path, len_m1) == 0)
                    return listener;
            }
        }
    }

    // not found
    return NULL;
}

void ICACHE_FLASH_ATTR sscp_close_listener(sscp_listener *listener)
{
    listener->hdr.type = TYPE_UNUSED;
}

sscp_connection ICACHE_FLASH_ATTR *sscp_get_connection(int i)
{
    if (i < SSCP_LISTENER_MAX || i >= SSCP_LISTENER_MAX + SSCP_CONNECTION_MAX)
        return NULL;
    return &sscp_connections[i - SSCP_LISTENER_MAX];
}

sscp_connection ICACHE_FLASH_ATTR *sscp_allocate_connection(int type, sscp_dispatch *dispatch)
{
    int i;
    for (i = 0; i < SSCP_CONNECTION_MAX; ++i) {
        sscp_connection *connection = &sscp_connections[i];
        if (connection->hdr.type == TYPE_UNUSED) {
            connection->hdr.type = type;
            connection->hdr.dispatch = dispatch;
            connection->flags = CONNECTION_INIT;
            connection->rxCount = 0;
            connection->rxIndex = 0;
            connection->txCount = 0;
            connection->txIndex = 0;
            os_memset(&connection->d, 0, sizeof(connection->d));
            return connection;
        }
    }
    return NULL;
}

void ICACHE_FLASH_ATTR sscp_close_connection(sscp_connection *connection)
{
    if (connection->hdr.type != TYPE_UNUSED) {
os_printf("closing connection %d\n", connection->hdr.index);
        if (connection->hdr.dispatch->close)
            (*connection->hdr.dispatch->close)((sscp_hdr *)connection);
os_printf(" done closing connection %d\n", connection->hdr.index);
        connection->hdr.type = TYPE_UNUSED;
    }
}

void ICACHE_FLASH_ATTR sscp_sendResponse(char *fmt, ...)
{
    char buf[128];
    int cnt;

    // insert the header
    buf[0] = sscp_start;
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

    if (flashConfig.sscp_pause_time_ms > 0) {
        char *p = buf;
        while (--cnt >= 0) {
            int byte = *p++;
            int i;
            uart_tx_one_char(UART0, byte);
            for (i = 0; i < flashConfig.sscp_need_pause_cnt; ++i) {
                if (byte == flashConfig.sscp_need_pause[i]) {
//os_printf("Delaying after '%c' 0x%02x for %d MS\n", byte, byte, sscp_pauseTimeMS);
                    uart_drain_tx_buffer(UART0);
                    os_delay_us(flashConfig.sscp_pause_time_ms * 1000);
                }
            }
        }
    }
    else {
        uart_tx_buffer(UART0, buf, cnt);
    }
    
    sscp_processing = 0;
}

void ICACHE_FLASH_ATTR sscp_sendPayload(char *buf, int cnt)
{
    uart_tx_buffer(UART0, buf, cnt);
}

typedef struct {
    char *cmd;
    int token;
    void (*handler)(int argc, char *argv[]);
} cmd_def;
static cmd_def cmds[] = {
{   "",                 -1,                     cmds_do_nothing     },
{   "JOIN",             SSCP_TKN_JOIN,          cmds_do_join        },
{   "CHECK",            SSCP_TKN_CHECK,         cmds_do_get         },
{   "SET",              SSCP_TKN_SET,           cmds_do_set         },
{   "LISTEN",           SSCP_TKN_LISTEN,        cmds_do_listen      },
{   "POLL",             SSCP_TKN_POLL,          cmds_do_poll        },
{   "PATH",             SSCP_TKN_PATH,          cmds_do_path        },
{   "SEND",             SSCP_TKN_SEND,          cmds_do_send        },
{   "RECV",             SSCP_TKN_RECV,          cmds_do_recv        },
{   "CLOSE",            SSCP_TKN_CLOSE,         cmds_do_close       },
{   "ARG",              SSCP_TKN_ARG,           http_do_arg         },
{   "REPLY",            SSCP_TKN_REPLY,         http_do_reply       },
{   "CONNECT",          SSCP_TKN_CONNECT,       tcp_do_connect      },
{   NULL,               0,                      NULL                }
};

/*
    case SSCP_TKN_INT8:
    case SSCP_TKN_UINT8:
    case SSCP_TKN_INT16:
    case SSCP_TKN_UINT16:
    case SSCP_TKN_INT32:
    case SSCP_TKN_UINT32:
*/
static void ICACHE_FLASH_ATTR sscp_process(char *buf, short len)
{
    char *argv[SSCP_MAX_ARGS + 1], tknbuf[2];
    cmd_def *def = NULL;
    int argc, tkn, i;
    char *p, *next;
    
#ifdef DUMP_CMDS
    dump("sscp", (uint8_t *)buf, len);
#endif
    
    p = buf;
    argc = 0;
    tkn = *(uint8_t *)p;
    
    if (tkn >= SSCP_MIN_TOKEN) {
    
        for (i = 0; cmds[i].cmd; ++i) {
            if (tkn == cmds[i].token) {
                def = &cmds[i];
                break;
            }
        }
        
        if (!def) {
            os_printf("No handler for 0x%02x\n", tkn);
            sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_REQUEST);
            return;
        }
        
        tknbuf[0] = tkn; // this may be needed to choose between terse and verbose responses
        tknbuf[1] = '\0';
        argv[argc++] = tknbuf;
        ++p;
    }
    
    else {
    
        if (!(next = os_strchr(p, ':')))
            next = &p[os_strlen(p)];
        else
            *next++ = '\0';
                    
        argv[argc++] = p;
        p = next;
        
        for (i = 0; cmds[i].cmd; ++i) {
            if (strcmp(argv[0], cmds[i].cmd) == 0) {
                def = &cmds[i];
                break;
            }
        }
        
        if (!def) {
            os_printf("No handler for '%s'\n", argv[0]);
            sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_REQUEST);
            return;
        }
    }
    
    if (*p) {
    
        while ((next = os_strchr(p, ',')) != NULL) {
            if (argc < SSCP_MAX_ARGS)
                argv[argc++] = p;
            *next++ = '\0';
            p = next;
        }
    
        if (argc < SSCP_MAX_ARGS)
            argv[argc++] = p;
    }
        
    argv[argc] = NULL;
        
#ifdef DUMP_ARGS
    for (i = 0; i < argc; ++i)
        os_printf("argv[%d] = '%s'\n", i, argv[i]);
#endif

    sscp_processing = 1;
    os_printf("Calling '%s' handler\n", def->cmd);
    (*def->handler)(argc, argv);
}

void ICACHE_FLASH_ATTR sscp_filter(char *buf, short len, void (*outOfBand)(void *data, char *buf, short len), void *data)
{
    uint8_t *p = (uint8_t *)buf;
    uint8_t *start = p;
    
    if (!flashConfig.sscp_enable) {
        (*outOfBand)(data, (char *)buf, len);
        return;
    }

#ifdef DUMP_FILTER
    dump("filter", p, len);
#endif

    while (--len >= 0) {
        if (sscp_payload) {
            *sscp_payload++ = *p++;
            if (--sscp_payload_remaining == 0) {
                sscp_payload = NULL;
                (*sscp_payload_cb)(sscp_payload_data, sscp_payload_length);
            }
            start = p;
        }
        else if (sscp_inside) {
            if (*p == '\r') {
                sscp_buffer[sscp_length] = '\0';
                sscp_process((char *)sscp_buffer, sscp_length);
                sscp_inside = 0;
                start = ++p;
            }
            else if (sscp_length < SSCP_BUFFER_MAX)
                sscp_buffer[sscp_length++] = *p++;
            else {
                os_printf("SSCP: command too long\n");
                sscp_inside = 0;
                start = p++;
            }
        }
        else {
            if (*p == sscp_start) {
                if (sscp_processing) {
                    os_printf("SSCP: busy processing a command\n");
                    ++p;
                    continue;
                }
                if (p > start && outOfBand) {
#ifdef DUMP_OUTOFBAND
                    dump("outOfBand", start, p - start);
#endif
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
#ifdef DUMP_OUTOFBAND
        dump("outOfBand", start, p - start);
#endif
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

