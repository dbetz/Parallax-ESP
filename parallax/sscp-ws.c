#include "esp8266.h"
#include "sscp.h"
#include "config.h"

static void send_connect_event(sscp_connection *connection, int prefix);
static void send_disconnect_event(sscp_connection *connection, int prefix);
static void send_data_event(sscp_connection *connection, int prefix);
static int checkForEvents_handler(sscp_hdr *hdr);
static void path_handler(sscp_hdr *hdr); 
static void send_handler(sscp_hdr *hdr, int size);
static void recv_handler(sscp_hdr *hdr, int size);
static void close_handler(sscp_hdr *hdr);

static sscp_dispatch wsDispatch = {
    .checkForEvents = checkForEvents_handler,
    .path = path_handler,
    .send = send_handler,
    .recv = recv_handler,
    .close = close_handler
};

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
        if (flashConfig.sscp_events)
            send_data_event(connection, '!');
    }
}

static void ICACHE_FLASH_ATTR websocketSentCb(Websock *ws)
{
	sscp_connection *connection = (sscp_connection *)ws->userData;
    connection->flags |= CONNECTION_TXDONE; 
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
    if (!(listener = sscp_find_listener(ws->conn->url, TYPE_WEBSOCKET_LISTENER))) {
        cgiWebsocketClose(ws, 0);
        return;
    }

    // find an unused connection
    if (!(connection = sscp_allocate_connection(TYPE_WEBSOCKET_CONNECTION, &wsDispatch))) {
        cgiWebsocketClose(ws, 0);
        return;
    }
    connection->listenerHandle = listener->hdr.handle;
    connection->d.ws.ws = ws;

    sscp_log("sscp_websocketConnect: url '%s'", ws->conn->url);
    ws->recvCb = websocketRecvCb;
    ws->sentCb = websocketSentCb;
    ws->closeCb = websocketCloseCb;
    ws->userData = connection;
}

static void ICACHE_FLASH_ATTR send_connect_event(sscp_connection *connection, int prefix)
{
    connection->flags &= ~CONNECTION_INIT;
    sscp_sendResponse("W,%d,%d", connection->hdr.handle, connection->listenerHandle);
}

static void ICACHE_FLASH_ATTR send_disconnect_event(sscp_connection *connection, int prefix)
{
    connection->flags &= ~CONNECTION_TERM;
    sscp_sendResponse("X,%d,0", connection->hdr.handle);
}

static void ICACHE_FLASH_ATTR send_data_event(sscp_connection *connection, int prefix)
{
    sscp_sendResponse("D,%d,%d", connection->hdr.handle, connection->rxCount);
}

static int ICACHE_FLASH_ATTR checkForEvents_handler(sscp_hdr *hdr)
{
    sscp_connection *connection = (sscp_connection *)hdr;
    
    if (!connection->d.ws.ws)
        return 0;
    
    if (connection->flags & CONNECTION_TERM) {
        send_disconnect_event(connection, '=');
        return 1;
    }
    
    else if (connection->flags & CONNECTION_INIT) {
        send_connect_event(connection, '=');
        return 1;
    }
    
    else if (connection->flags & CONNECTION_RXFULL) {
        send_data_event(connection, '=');
        return 1;
    }
    
    return 0;
}

static void ICACHE_FLASH_ATTR path_handler(sscp_hdr *hdr)
{
    sscp_connection *connection = (sscp_connection *)hdr;
    Websock *ws = (Websock *)connection->d.ws.ws;
    
    if (!ws || !ws->conn) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
        return;
    }
    
    sscp_sendResponse("S,%s", ws->conn->url);
}

// this is called after all of the data for a SEND has been received from the MCU
static void ICACHE_FLASH_ATTR send_cb(void *data, int count)
{
    sscp_connection *connection = (sscp_connection *)data;
    Websock *ws = (Websock *)connection->d.ws.ws;

    char sendBuff[1024];
    httpdSetSendBuffer(ws->conn, sendBuff, sizeof(sendBuff));
    cgiWebsocketSend(ws, connection->txBuffer, count, WEBSOCK_FLAG_NONE);
    connection->flags &= ~CONNECTION_TXFULL;

    sscp_sendResponse("S,%d", count);
}

static void ICACHE_FLASH_ATTR send_handler(sscp_hdr *hdr, int size)
{
    sscp_connection *connection = (sscp_connection *)hdr;
    
    if (size == 0)
        sscp_sendResponse("S,0");
    else {
        // response is sent by tcp_sent_cb
        sscp_capturePayload(connection->txBuffer, size, send_cb, connection);
        connection->flags |= CONNECTION_TXFULL;
    }
}

static void ICACHE_FLASH_ATTR recv_handler(sscp_hdr *hdr, int size)
{
    sscp_connection *connection = (sscp_connection *)hdr;
    
    if (!(connection->flags & CONNECTION_RXFULL)) {
        sscp_sendResponse("S,0");
        return;
    }

    if (connection->rxIndex + size > connection->rxCount)
        size = connection->rxCount - connection->rxIndex;

    sscp_sendResponse("S,%d", size);
    if (size > 0) {
        sscp_sendPayload(connection->rxBuffer + connection->rxIndex, size);
        connection->rxIndex += size;
    }
    
    if (connection->rxIndex >= connection->rxCount)
        connection->flags &= ~CONNECTION_RXFULL;
}

static void ICACHE_FLASH_ATTR close_handler(sscp_hdr *hdr)
{
    sscp_connection *connection = (sscp_connection *)hdr;
    Websock *ws = connection->d.ws.ws;
    if (ws)
        cgiWebsocketClose(ws, 0);
}
