#include "esp8266.h"
#include "sscp.h"
#include "httpd.h"

static void send_connect_event(sscp_connection *connection, int prefix);
static void send_disconnect_event(sscp_connection *connection, int prefix);
static void send_data_event(sscp_connection *connection, int prefix);
static int checkForEvents_handler(sscp_hdr *hdr);
static void path_handler(sscp_hdr *hdr); 
static void send_handler(sscp_hdr *hdr, int size);
static void recv_handler(sscp_hdr *hdr, int size);
static void close_handler(sscp_hdr *hdr);

static sscp_dispatch httpDispatch = {
    .checkForEvents = checkForEvents_handler,
    .path = path_handler,
    .send = send_handler,
    .recv = recv_handler,
    .close = close_handler
};

// ARG,chan
void ICACHE_FLASH_ATTR http_do_arg(int argc, char *argv[])
{
    char buf[128];
    sscp_connection *connection;
    HttpdConnData *connData;
    
    if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }

    if (connection->hdr.type != TYPE_HTTP_CONNECTION) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
    
    if (!(connData = (HttpdConnData *)connection->d.http.conn) || connData->conn == NULL) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
        return;
    }
    
    if (httpdFindArg(connData->getArgs, argv[2], buf, sizeof(buf)) >= 0) {
        sscp_sendResponse("S,%s", buf);
        return;
    }

    if (!connData->post->buff) {
        sscp_sendResponse("N,0");
        return;
    }
    
    if (httpdFindArg(connData->post->buff, argv[2], buf, sizeof(buf)) >= 0) {
        sscp_sendResponse("S,%s", buf);
        return;
    }

    sscp_sendResponse("N,0");
}

#define MAX_SENDBUFF_LEN 1024

static void ICACHE_FLASH_ATTR reply_cb(void *data, int count)
{
    sscp_connection *connection = (sscp_connection *)data;
    HttpdConnData *connData = connection->d.http.conn;
    
    char sendBuff[MAX_SENDBUFF_LEN];
    httpdSetSendBuffer(connData, sendBuff, sizeof(sendBuff));
    
sscp_log("REPLY payload callback: %d bytes of %d", count, connection->txCount);
    char buf[20];
    os_sprintf(buf, "%d", connection->txCount);
    
    httpdStartResponse(connData, connection->d.http.code);
    httpdHeader(connData, "Content-Length", buf);
    httpdEndHeaders(connData);
    httpdSend(connData, connection->txBuffer, count);
    httpdFlushSendBuffer(connData);
    
    connection->flags &= ~CONNECTION_TXFULL;
    sscp_sendResponse("S,%d", connection->d.http.count);
    
    connection->d.http.count = count;
}

// REPLY,chan,code[,payload-size]
void ICACHE_FLASH_ATTR http_do_reply(int argc, char *argv[])
{
    sscp_connection *connection;
    HttpdConnData *connData;
    int count;

    if (argc < 3 || argc > 5) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }

    if (connection->hdr.type != TYPE_HTTP_CONNECTION) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
        
    if (!(connData = (HttpdConnData *)connection->d.http.conn) || connData->conn == NULL) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
        return;
    }
    
    connection->d.http.code = atoi(argv[2]);
    
    connection->txCount = (argc > 3 ? atoi(argv[3]) : 0);
    count = (argc > 4 ? atoi(argv[4]) : connection->txCount);
    connection->txIndex = 0;
sscp_log("REPLY: total %d, this %d", connection->txCount, count);

    if (connection->txCount < 0
    ||  count < 0
    ||  connection->txCount < count
    ||  count > SSCP_TX_BUFFER_MAX) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_SIZE);
        return;
    }
    
    // response is sent by reply_cb
    if (connection->txCount == 0)
        reply_cb(connection, 0);
    else {
        sscp_capturePayload(connection->txBuffer, count, reply_cb, connection);
        connection->flags |= CONNECTION_TXFULL;
    }
}

int ICACHE_FLASH_ATTR cgiSSCPHandleRequest(HttpdConnData *connData)
{
    sscp_connection *connection = (sscp_connection *)connData->cgiData;
    sscp_listener *listener;
    
    // check for the cleanup call
    if (connData->conn == NULL) {
        if (connection) {
            connection->flags |= CONNECTION_TERM;
            if (sscp_sendEvents)
                send_disconnect_event(connection, '!');
        }
        return HTTPD_CGI_DONE;
    }
    
    // check to see if this request is already in progress
    if (connection) {
        
sscp_log("REPLY send complete");    
        if ((connection->txIndex += connection->d.http.count) < connection->txCount)
            return HTTPD_CGI_MORE;

sscp_log("REPLY complete");
        connection->hdr.type = TYPE_UNUSED;
        return HTTPD_CGI_DONE;
    }
    
    // find a matching listener
    if (!(listener = sscp_find_listener(connData->url, TYPE_HTTP_LISTENER)))
        return HTTPD_CGI_NOTFOUND;

    // allocate a connection
    if (!(connection = sscp_allocate_connection(TYPE_HTTP_CONNECTION, &httpDispatch))) {
        httpdStartResponse(connData, 400);
        httpdEndHeaders(connData);
os_printf("sscp: no connections available for %s request\n", connData->url);
        httpdSend(connData, "No connections available", -1);
        return HTTPD_CGI_DONE;
    }
    connection->listenerHandle = listener->hdr.handle;
    connData->cgiData = connection;
    connection->d.http.conn = connData;

sscp_log("sscp: %d handling %s request", connection->hdr.handle, connData->url);
    if (sscp_sendEvents)
        send_connect_event(connection, '!');
        
    return HTTPD_CGI_MORE;
}

static void ICACHE_FLASH_ATTR send_connect_event(sscp_connection *connection, int prefix)
{
    connection->flags &= ~CONNECTION_INIT;
    HttpdConnData *connData = (HttpdConnData *)connection->d.http.conn;
    if (connData) {
        switch (connData->requestType) {
        case HTTPD_METHOD_GET:
            sscp_send(prefix, "G,%d,%d", connection->hdr.handle, connection->listenerHandle);
            break;
        case HTTPD_METHOD_POST:
            sscp_send(prefix, "P,%d,%d", connection->hdr.handle, connection->listenerHandle);
            break;
        default:
            sscp_send(prefix, "E,%d,%d", SSCP_ERROR_INVALID_METHOD, connData->requestType);
            break;
        }
    }
}

static void ICACHE_FLASH_ATTR send_disconnect_event(sscp_connection *connection, int prefix)
{
    HttpdConnData *connData = (HttpdConnData *)connection->d.http.conn;
    if (connData) {
        switch (connData->requestType) {
        case HTTPD_METHOD_GET:
            sscp_send(prefix, "G,%d,0", connection->hdr.handle);
            break;
        case HTTPD_METHOD_POST:
            sscp_send(prefix, "P,%d,0", connection->hdr.handle);
            break;
        default:
            sscp_send(prefix, "E,%d,%d", SSCP_ERROR_INVALID_METHOD, connData->requestType);
            break;
        }
    }
    sscp_close_connection(connection);
}

static void ICACHE_FLASH_ATTR send_data_event(sscp_connection *connection, int prefix)
{
    sscp_send(prefix, "D,%d,%d", connection->hdr.handle, connection->listenerHandle);
}

static int ICACHE_FLASH_ATTR checkForEvents_handler(sscp_hdr *hdr)
{
    sscp_connection *connection = (sscp_connection *)hdr;
    
    if (!connection->d.http.conn)
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
    HttpdConnData *connData = (HttpdConnData *)connection->d.http.conn;
    
    if (!connData || !connData->conn) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
        return;
    }
    
    sscp_sendResponse("S,%s", connData->url);
}

static void ICACHE_FLASH_ATTR send_cb(void *data, int count)
{
    sscp_connection *connection = (sscp_connection *)data;
    HttpdConnData *connData = connection->d.http.conn;
sscp_log("  captured %d bytes", count);
    
    httpdUnbufferedSend(connData, connection->txBuffer, count);
    
    connection->flags &= ~CONNECTION_TXFULL;
    sscp_sendResponse("S,%d", connection->d.http.count);

    connection->d.http.count = count;
}

static void ICACHE_FLASH_ATTR send_handler(sscp_hdr *hdr, int size)
{
    sscp_connection *connection = (sscp_connection *)hdr;
    
    if (connection->txIndex + size > connection->txCount) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_SIZE);
        return;
    }
    
    if (size == 0)
        sscp_sendResponse("S,0");
    else {
        // response is sent by send_cb
        sscp_capturePayload(connection->txBuffer, size, send_cb, connection);
        connection->flags |= CONNECTION_TXFULL;
    }
}

static void ICACHE_FLASH_ATTR recv_handler(sscp_hdr *hdr, int size)
{
    sscp_connection *connection = (sscp_connection *)hdr;
    HttpdConnData *connData;
    int rxCount;

    if (!(connData = (HttpdConnData *)connection->d.http.conn) || connData->conn == NULL) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
        return;
    }
    
    rxCount = connData->post->buff ? connData->post->len : 0;

    if (connection->rxIndex + size > rxCount)
        size = rxCount - connection->rxIndex;

    sscp_sendResponse("S,%d", size);
    if (size > 0) {
        sscp_sendPayload(connection->rxBuffer + connection->rxIndex, size);
        connection->rxIndex += size;
    }
}

static void ICACHE_FLASH_ATTR close_handler(sscp_hdr *hdr)
{
    sscp_connection *connection = (sscp_connection *)hdr;
    HttpdConnData *connData = connection->d.http.conn;
    if (connData)
        connData->cgi = NULL;
}
