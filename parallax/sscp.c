/*
    sscp.c - Simple Serial Command Protocol

	Copyright (c) 2016 Parallax Inc.
    See the file LICENSE.txt for licensing information.
*/

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

enum {
    STATE_IDLE,
    STATE_PARSING,
    STATE_COLLECTING,
    STATE_PAYLOAD
};

static int sscp_state;
static int sscp_collect;
static int sscp_token;
static int sscp_separator;
static uint8_t sscp_buffer[SSCP_BUFFER_MAX + 16]; // add some extra space for os_sprintf of numeric tokens
static int sscp_length;

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
        sscp_listeners[i].hdr.handle = i + 1;
    
    os_memset(&sscp_connections, 0, sizeof(sscp_connections));
    for (i = 0; i < SSCP_CONNECTION_MAX; ++i)
        sscp_connections[i].hdr.handle = SSCP_LISTENER_MAX + i + 1;
    
    sscp_reset();
}

void ICACHE_FLASH_ATTR sscp_reset(void)
{
    int i;
    
    for (i = 0; i < SSCP_LISTENER_MAX; ++i)
        sscp_close_listener(&sscp_listeners[i]);
    for (i = 0; i < SSCP_CONNECTION_MAX; ++i)
        sscp_close_connection(&sscp_connections[i]);
        
    sscp_processing = 0;
    sscp_state = STATE_IDLE;
    sscp_separator = -1;
    sscp_length = 0;
    sscp_payload = NULL;
    sscp_payload_length = 0;
    sscp_payload_remaining = 0;
}

void ICACHE_FLASH_ATTR sscp_capturePayload(char *buf, int length, void (*cb)(void *data, int count), void *data)
{
    sscp_payload = buf;
    sscp_payload_length = length;
    sscp_payload_remaining = length;
    sscp_payload_cb = cb;
    sscp_payload_data = data;
    sscp_state = STATE_PAYLOAD;
}

sscp_hdr ICACHE_FLASH_ATTR *sscp_get_handle(int i)
{
    sscp_hdr *hdr;
    
    if (i >= 1 && i <= SSCP_LISTENER_MAX)
        hdr = (sscp_hdr *)&sscp_listeners[i - 1];
    else if (i >= SSCP_LISTENER_MAX + 1 && i <= SSCP_LISTENER_MAX + SSCP_CONNECTION_MAX)
        hdr = (sscp_hdr *)&sscp_connections[i - SSCP_LISTENER_MAX - 1];
    else
        return NULL;

    return hdr->type == TYPE_UNUSED ? NULL : hdr;
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

            // check for a literal match
            if (os_strcmp(listener->path, path) == 0) {
sscp_log("listener: matching '%s' with '%s'", listener->path, path);
                return listener;
            }
            
            // check for a wildcard match
            else {
                int len_m1 = os_strlen(listener->path) - 1;
                if (listener->path[len_m1] == '*' && os_strncmp(listener->path, path, len_m1) == 0) {
sscp_log("listener: matching '%s' with '%s'", listener->path, path);
                    return listener;
                }
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
    sscp_connection *connection;
    
    if (i >= SSCP_LISTENER_MAX + 1 && i <= SSCP_LISTENER_MAX + SSCP_CONNECTION_MAX)
        connection = &sscp_connections[i - SSCP_LISTENER_MAX - 1];
    else
        return NULL;

    return connection->hdr.type == TYPE_UNUSED ? NULL : connection;
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
            connection->listenerHandle = 0;
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
        if (connection->hdr.dispatch->close)
            (*connection->hdr.dispatch->close)((sscp_hdr *)connection);
        connection->hdr.type = TYPE_UNUSED;
    }
}

static void ICACHE_FLASH_ATTR sendToMCU(int prefix, char *fmt, va_list ap)
{
    char buf[128];
    int cnt;

    // insert the header
    buf[0] = flashConfig.sscp_start;
    buf[1] = prefix;

    // insert the formatted response
    cnt = ets_vsnprintf(&buf[2], sizeof(buf) - 3, fmt, ap);

    // check to see if the response was truncated
    if (cnt >= sizeof(buf) - 3)
        cnt = sizeof(buf) - 3 - 1;

    // display the response before inserting the final \r
    sscp_log("%s: '%s'", buf[1] == '!' ? "Event" : "Reply", &buf[1]);

    // terminate the response with a \r
    buf[2 + cnt] = '\r';
    cnt += 3;

    // handle inserting pauses after certain characters
    if (flashConfig.sscp_pause_time_ms > 0) {
        char *p = buf;
        while (--cnt >= 0) {
            int needPause = 0;
            int byte = *p++;
            uart_tx_one_char(UART0, byte);
            if (byte == '\r')
                needPause = 1;
            else {
                int i;
                for (i = 0; i < flashConfig.sscp_need_pause_cnt; ++i) {
                    if (byte == flashConfig.sscp_need_pause[i]) {
                        needPause = 1;
                        break;
                    }
                }
            }
            if (needPause) {
                uart_drain_tx_buffer(UART0);
                os_delay_us(flashConfig.sscp_pause_time_ms * 1000);
            }
        }
    }

    // no pauses after characters needed
    else {
        uart_tx_buffer(UART0, buf, cnt);
    }
    
    sscp_processing = 0;
}

void ICACHE_FLASH_ATTR sscp_send(int prefix, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sendToMCU(prefix, fmt, ap);
    va_end(ap);
}

void ICACHE_FLASH_ATTR sscp_sendResponse(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sendToMCU('=', fmt, ap);
    va_end(ap);
}

void ICACHE_FLASH_ATTR sscp_sendPayload(char *buf, int cnt)
{
    uart_tx_buffer(UART0, buf, cnt);
}

void ICACHE_FLASH_ATTR sscp_sendEvent(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sendToMCU('!', fmt, ap);
    va_end(ap);
}

void ICACHE_FLASH_ATTR sscp_log(char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    ets_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    os_printf("[%u] %s\n", system_get_time() / 1000, buf);
}

typedef struct {
    char *cmd;
    void (*handler)(int argc, char *argv[]);
} cmd_def;
static cmd_def cmds[] = {
{   "",                 cmds_do_nothing     },
{   "JOIN",             cmds_do_join        },
{   "CHECK",            cmds_do_get         },
{   "SET",              cmds_do_set         },
{   "LISTEN",           cmds_do_listen      },
{   "POLL",             cmds_do_poll        },
{   "PATH",             cmds_do_path        },
{   "SEND",             cmds_do_send        },
{   "RECV",             cmds_do_recv        },
{   "CLOSE",            cmds_do_close       },
{   "ARG",              http_do_arg         },
{   "REPLY",            http_do_reply       },
{   "CONNECT",          tcp_do_connect      },
{   "APSCAN",           wifi_do_apscan      },
{   "APGET",            wifi_do_apget       },
{   NULL,               NULL                }
};

static void ICACHE_FLASH_ATTR sscp_process(char *buf, short len)
{
    char *argv[SSCP_MAX_ARGS + 1];
    cmd_def *def = NULL;
    char *p, *next;
    int argc, i;
    
#ifdef DUMP_CMDS
    dump("sscp", (uint8_t *)buf, len);
#endif
    
    p = buf;
    argc = 0;
    
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
    sscp_log("Calling '%s' handler", def->cmd);
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
        switch (sscp_state) {
        case STATE_IDLE:
            if (*p == flashConfig.sscp_start) {
                if (sscp_processing) {
                    sscp_log("SSCP: busy processing a command");
                    ++p;
                    continue;
                }
                if (p > start) {
#ifdef DUMP_OUTOFBAND
                    dump("outOfBand", start, p - start);
#endif
                    if (outOfBand)
                        (*outOfBand)(data, (char *)start, p - start);
                }
                sscp_state = STATE_PARSING;
                sscp_separator = -1;
                sscp_length = 0;
                ++p;
            }
            else {
                // just accumulate data outside of a command
                ++p;
            }
            break;
        case STATE_PARSING:
            if (*p != '\r' && sscp_separator != -1) {
                if (sscp_length < SSCP_BUFFER_MAX) {
                    sscp_buffer[sscp_length++] = sscp_separator;
                    sscp_separator = -1;
                }
                else {
                    os_printf("SSCP: command too long\n");
                    sscp_state = STATE_IDLE;
                    start = p++;
                }
            }
            switch (*p) {
            case '\r':
                sscp_buffer[sscp_length] = '\0';
                sscp_state = STATE_IDLE; // could be changed to STATE_COLLECTING by handler
                sscp_process((char *)sscp_buffer, sscp_length);
                start = ++p;
                break;
            case SSCP_TKN_INT8:
            case SSCP_TKN_UINT8:
                sscp_token = *p++;
                sscp_state = STATE_COLLECTING;
                sscp_collect = 1;
                break;
            case SSCP_TKN_INT16:
            case SSCP_TKN_UINT16:
                sscp_token = *p++;
                sscp_state = STATE_COLLECTING;
                sscp_collect = 2;
                break;
            case SSCP_TKN_INT32:
            case SSCP_TKN_UINT32:
                sscp_token = *p++;
                sscp_state = STATE_COLLECTING;
                sscp_collect = 4;
                break;
            case SSCP_TKN_JOIN:
            case SSCP_TKN_CHECK:
            case SSCP_TKN_SET:
            case SSCP_TKN_POLL:
            case SSCP_TKN_PATH:
            case SSCP_TKN_SEND:
            case SSCP_TKN_RECV:
            case SSCP_TKN_CLOSE:
            case SSCP_TKN_LISTEN:
            case SSCP_TKN_ARG:
            case SSCP_TKN_REPLY:
            case SSCP_TKN_CONNECT:
            case SSCP_TKN_HTTP:
            case SSCP_TKN_WS:
            case SSCP_TKN_TCP:
            case SSCP_TKN_STA:
            case SSCP_TKN_AP:
            case SSCP_TKN_STA_AP:
                {
                    int length, sep;
                    char *name;
                    switch (*p++) {
                    case SSCP_TKN_JOIN:     name = "HTTP";    sep = ':'; break;
                    case SSCP_TKN_CHECK:    name = "CHECK";   sep = ':'; break;
                    case SSCP_TKN_SET:      name = "SET";     sep = ':'; break;
                    case SSCP_TKN_POLL:     name = "POLL";    sep = ':'; break;
                    case SSCP_TKN_PATH:     name = "PATH";    sep = ':'; break;
                    case SSCP_TKN_SEND:     name = "SEND";    sep = ':'; break;
                    case SSCP_TKN_RECV:     name = "RECV";    sep = ':'; break;
                    case SSCP_TKN_CLOSE:    name = "CLOSE";   sep = ':'; break;
                    case SSCP_TKN_LISTEN:   name = "LISTEN";  sep = ':'; break;
                    case SSCP_TKN_ARG:      name = "ARG";     sep = ':'; break;
                    case SSCP_TKN_REPLY:    name = "REPLY";   sep = ':'; break;
                    case SSCP_TKN_CONNECT:  name = "CONNECT"; sep = ':'; break;
                    case SSCP_TKN_HTTP:     name = "HTTP";    sep = ','; break;
                    case SSCP_TKN_WS:       name = "WS";      sep = ','; break;
                    case SSCP_TKN_TCP:      name = "TCP";     sep = ','; break;
                    case SSCP_TKN_STA:      name = "STA";     sep = ','; break;
                    case SSCP_TKN_AP:       name = "AP";      sep = ','; break;
                    case SSCP_TKN_STA_AP:   name = "STA+AP";  sep = ','; break;
                    default:
                        // internal error
                        name = "";
                        sep = -1;
                        break;
                    }
                    length = os_strlen(name);
                    if (sscp_length + length < SSCP_BUFFER_MAX) {
                        os_strcpy((char *)&sscp_buffer[sscp_length], name);
                        sscp_length += length;
                        sscp_separator = sep;
                    }
                    else {
                        os_printf("SSCP: command too long\n");
                        sscp_state = STATE_IDLE;
                        start = p++;
                    }
                }
                break;
            default:
                if (sscp_length < SSCP_BUFFER_MAX)
                    sscp_buffer[sscp_length++] = *p++;
                else {
                    os_printf("SSCP: command too long\n");
                    sscp_state = STATE_IDLE;
                    start = p++;
                }
                break;
            }
            break;
        case STATE_COLLECTING:
            if (sscp_length < SSCP_BUFFER_MAX)
                sscp_buffer[sscp_length++] = *p++;
            else {
                os_printf("SSCP: command too long\n");
                sscp_state = STATE_IDLE;
                start = p++;
                continue;
            }
            if (--sscp_collect == 0) {
                switch (sscp_token) {
                case SSCP_TKN_INT8:
                    {
                        int8_t value = *(int8_t *)&sscp_buffer[sscp_length - 1];
                        os_sprintf((char *)&sscp_buffer[sscp_length - 1], "%d", value);
                    }
                    break;
                case SSCP_TKN_UINT8:
                    {
                        uint8_t value = *(uint8_t *)&sscp_buffer[sscp_length - 1];
                        os_sprintf((char *)&sscp_buffer[sscp_length - 1], "%u", value);
                    }
                    break;
                case SSCP_TKN_INT16:
                    {
                        int16_t value = *(int8_t *)&sscp_buffer[sscp_length - 2] << 8;
                        value|= *(uint8_t *)&sscp_buffer[sscp_length - 1];
                        os_sprintf((char *)&sscp_buffer[sscp_length - 2], "%d", value);
                    }
                    break;
                case SSCP_TKN_UINT16:
                    {
                        uint16_t value = *(uint8_t *)&sscp_buffer[sscp_length - 1] << 8;
                        value |= *(uint8_t *)&sscp_buffer[sscp_length - 2];
                        os_sprintf((char *)&sscp_buffer[sscp_length - 2], "%u", value);
                    }
                    break;
                case SSCP_TKN_INT32:
                    {
                        int32_t value = *(int8_t *)&sscp_buffer[sscp_length - 1] << 24;
                        value |= *(uint8_t *)&sscp_buffer[sscp_length - 2] << 16;
                        value |= *(uint8_t *)&sscp_buffer[sscp_length - 3] << 8;
                        value |= *(uint8_t *)&sscp_buffer[sscp_length - 4];
                        os_sprintf((char *)&sscp_buffer[sscp_length - 4], "%d", (int)value);
                    }
                    break;
                case SSCP_TKN_UINT32:
                    {
                        uint32_t value = *(uint8_t *)&sscp_buffer[sscp_length - 1] << 24;
                        value |= *(uint8_t *)&sscp_buffer[sscp_length - 2] << 16;
                        value |= *(uint8_t *)&sscp_buffer[sscp_length - 3] << 8;
                        value |= *(uint8_t *)&sscp_buffer[sscp_length - 4];
                        os_sprintf((char *)&sscp_buffer[sscp_length - 4], "%u", (unsigned int)value);
                    }
                    break;
                default:
                    // internal error
                    break;
                }
                sscp_length = os_strlen((char *)sscp_buffer);
                if (sscp_length > SSCP_BUFFER_MAX) {
                    os_printf("SSCP: command too long\n");
                    sscp_state = STATE_IDLE;
                    start = p;
                    continue;
                }
                sscp_state = STATE_PARSING;
                sscp_separator = ',';
            }
            break;
        case STATE_PAYLOAD:
            *sscp_payload++ = *p++;
            if (--sscp_payload_remaining == 0) {
                (*sscp_payload_cb)(sscp_payload_data, sscp_payload_length);
                sscp_state = STATE_IDLE;
            }
            start = p;
            break;
        default:
            // internal error
            break;
        }
    }
    
    if (sscp_state == STATE_IDLE && p > start) {
#ifdef DUMP_OUTOFBAND
        dump("outOfBand", start, p - start);
#endif
        if (outOfBand)
            (*outOfBand)(data, (char *)start, p - start);
    }
}

#ifdef DUMP
static void ICACHE_FLASH_ATTR dump(char *tag, uint8_t *buf, int len)
{
    os_printf("%s[%d]: '", tag, len);
    for (int i = 0; i < len; ++i)
        os_printf("%c", buf[i]);
    os_printf("'\n");
}
#endif

