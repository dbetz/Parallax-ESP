#include "esp8266.h"
#include "sscp.h"

// WSLISTEN,chan,path
void ICACHE_FLASH_ATTR ws_do_wslisten(int argc, char *argv[])
{
    sscp_listener *listener;

    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (!(listener = sscp_get_listener(atoi(argv[1])))) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (os_strlen(argv[2]) >= SSCP_PATH_MAX) {
        sscp_sendResponse("ERROR");
        return;
    }

    sscp_close_listener(listener);

    os_printf("Listening for '%s'\n", argv[2]);
    os_strcpy(listener->path, argv[2]);
    listener->type = LISTENER_WEBSOCKET;
    
    sscp_sendResponse("OK");
}

// WSREAD,chan
void ICACHE_FLASH_ATTR ws_do_wsread(int argc, char *argv[])
{
    sscp_connection *connection;

    if (argc != 2) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("ERROR");
        return;
    }

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
void ICACHE_FLASH_ATTR ws_do_wswrite(int argc, char *argv[])
{
    sscp_connection *connection;
    Websock *ws;

    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (!connection->listener || connection->listener->type != LISTENER_WEBSOCKET) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    ws = (Websock *)connection->d.ws.ws;

    char sendBuff[1024];
    httpdSetSendBuffer(ws->conn, sendBuff, sizeof(sendBuff));
    cgiWebsocketSend(ws, argv[2], os_strlen(argv[2]), WEBSOCK_FLAG_NONE);

    sscp_sendResponse("OK");
}

static void ICACHE_FLASH_ATTR websocketRecvCb(Websock *ws, char *data, int len, int flags)
{
	sscp_connection *connection = (sscp_connection *)ws->userData;
    if (!(connection->flags & CONNECTION_RXFULL)) {
        if (len > SSCP_RX_BUFFER_MAX)
            len = SSCP_RX_BUFFER_MAX;
        os_memcpy(connection->rxBuffer, data, len);
        connection->rxCount = len;
        connection->flags |= CONNECTION_RXFULL;
    }
}

static void ICACHE_FLASH_ATTR websocketSentCb(Websock *ws)
{
}

static void ICACHE_FLASH_ATTR websocketCloseCb(Websock *ws)
{
	sscp_connection *connection = (sscp_connection *)ws->userData;
    connection->d.ws.ws = NULL;
}

void ICACHE_FLASH_ATTR sscp_websocketConnect(Websock *ws)
{
    sscp_listener *listener;
    sscp_connection *connection;
    
    // find a matching listener
    if (!(listener = sscp_find_listener(ws->conn->url, LISTENER_WEBSOCKET))) {
        cgiWebsocketClose(ws, 0);
        return;
    }

    // find an unused connection
    if (!(connection = sscp_allocate_connection(listener))) {
        cgiWebsocketClose(ws, 0);
        return;
    }
    connection->d.ws.ws = ws;

    os_printf("sscp_websocketConnect: url '%s'\n", ws->conn->url);
    ws->recvCb = websocketRecvCb;
    ws->sentCb = websocketSentCb;
    ws->closeCb = websocketCloseCb;
    ws->userData = connection;
}

