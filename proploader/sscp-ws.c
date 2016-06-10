#include "esp8266.h"
#include "sscp.h"

// WSLISTEN,chan,path
void ICACHE_FLASH_ATTR ws_do_wslisten(int argc, char *argv[])
{
    sscp_listener *listener;

    if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }

    if (!(listener = sscp_get_listener(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }

    if (os_strlen(argv[2]) >= SSCP_PATH_MAX) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_SIZE);
        return;
    }

    sscp_close_listener(listener);

    os_printf("Listening for '%s'\n", argv[2]);
    os_strcpy(listener->path, argv[2]);
    listener->type = LISTENER_WEBSOCKET;
    
    sscp_sendResponse("S,0");
}

static void ICACHE_FLASH_ATTR send_cb(void *data)
{
    sscp_connection *connection = (sscp_connection *)data;
    Websock *ws = (Websock *)connection->d.ws.ws;

    char sendBuff[1024];
    httpdSetSendBuffer(ws->conn, sendBuff, sizeof(sendBuff));
    cgiWebsocketSend(ws, connection->txBuffer, connection->txCount, WEBSOCK_FLAG_NONE);
    connection->flags &= ~CONNECTION_TXFULL;

    sscp_sendResponse("S,0");
}

// helper for SEND,chan,count
void ICACHE_FLASH_ATTR ws_send_helper(sscp_connection *connection, int argc, char *argv[])
{
    if ((connection->txCount = atoi(argv[2])) < 0 || connection->txCount > SSCP_TX_BUFFER_MAX) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_SIZE);
        return;
    }
    
    if (connection->txCount == 0)
        sscp_sendResponse("S,0");
    else {
        // response is sent by tcp_sent_cb
        sscp_capturePayload(connection->txBuffer, connection->txCount, send_cb, connection);
        connection->flags |= CONNECTION_TXFULL;
    }
}

static void ICACHE_FLASH_ATTR websocketRecvCb(Websock *ws, char *data, int len, int flags)
{
	sscp_connection *connection = (sscp_connection *)ws->userData;
    if (!(connection->flags & CONNECTION_RXFULL)) {
        if (len > SSCP_RX_BUFFER_MAX)
            len = SSCP_RX_BUFFER_MAX;
        os_memcpy(connection->rxBuffer, data, len);
        connection->rxCount = len;
        connection->rxIndex = 0;
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

void ICACHE_FLASH_ATTR ws_disconnect(sscp_connection *connection)
{
    Websock *ws = connection->d.ws.ws;
    if (ws)
        cgiWebsocketClose(ws, 0);
    sscp_free_connection(connection);
}

