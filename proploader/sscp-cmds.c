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

// RECV,chan,size
void ICACHE_FLASH_ATTR cmds_do_recv(int argc, char *argv[])
{
    sscp_connection *connection;
    int size;

    if (argc != 3) {
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

    if ((size = atoi(argv[2])) < 0) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
    
    if (connection->rxIndex + size > connection->rxCount)
        size = connection->rxCount - connection->rxIndex;

    if (!(connection->flags & CONNECTION_RXFULL)) {
        sscp_sendResponse("N,0");
        return;
    }

    sscp_sendResponse("S,%d", size);
    if (size > 0) {
        sscp_sendPayload(connection->rxBuffer + connection->rxIndex, size);
        connection->rxIndex += size;
    }
    connection->flags &= ~CONNECTION_RXFULL;
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
