#include "esp8266.h"
#include "sscp.h"
#include "uart.h"
#include "config.h"
#include "cgiwifi.h"

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
{   "pause-time",       intGetHandler,  intSetHandler,  &sscp_pauseTimeMS           },
{   "enable-sscp",      intGetHandler,  intSetHandler,  &flashConfig.enable_sscp    },
{   "baud-rate",        intGetHandler,  setBaudrate,    &uart0_baudRate             },
{   NULL,               NULL,           NULL,           NULL                        }
};

// (nothing)
void ICACHE_FLASH_ATTR cmds_do_nothing(int argc, char *argv[])
{
    sscp_sendResponse("S,0");
}

// GET,var
void ICACHE_FLASH_ATTR cmds_do_get(int argc, char *argv[])
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
void ICACHE_FLASH_ATTR cmds_do_set(int argc, char *argv[])
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
void ICACHE_FLASH_ATTR cmds_do_poll(int argc, char *argv[])
{
    sscp_connection *connection;
    HttpdConnData *connData;
    Websock *ws;
    int i;

    if (argc != 1) {
        sscp_sendResponse("E:%,invalid arguments", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }

    for (i = 0, connection = sscp_connections; i < SSCP_CONNECTION_MAX; ++i, ++connection) {
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
                sscp_sendResponse("D:%d,%d", connection->index, connection->rxCount);
                return;
            }
        }
    }
    
    sscp_sendResponse("N:0,");
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
    sscp_sendPayload(connection->rxBuffer, connection->rxCount);
    connection->flags &= ~CONNECTION_RXFULL;
}

