#include "esp8266.h"
#include "sscp.h"
#include "httpd.h"

static void path_handler(sscp_hdr *hdr); 
static void send_handler(sscp_hdr *hdr, int size);
static void recv_handler(sscp_hdr *hdr, int size);
static void close_handler(sscp_hdr *hdr);

static sscp_dispatch httpDispatch = {
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
    
    if (connection->txIndex == 0) {
        char buf[20];
        os_sprintf(buf, "%d", connection->txCount);
        httpdStartResponse(connData, connection->d.http.code);
        httpdHeader(connData, "Content-Length", buf);
        httpdEndHeaders(connData);
    }
    
    httpdSend(connData, connection->txBuffer, count);
    httpdFlushSendBuffer(connData);

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
os_printf("REPLY: %d, %d\n", connection->txCount, count);

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
    sscp_listener *listener;
    sscp_connection *connection;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;
    
    // check to see if this request is already in progress
    if (connData->cgiData) {
        sscp_connection *connection = (sscp_connection *)connData->cgiData;
        
os_printf("  send complete\n");
        connection->flags &= ~CONNECTION_TXFULL;
        sscp_sendResponse("S,%d", connection->d.http.count);
    
        if ((connection->txIndex += connection->d.http.count) < connection->txCount)
            return HTTPD_CGI_MORE;

os_printf("  reply complete\n");
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
    connData->cgiData = connection;
    connection->d.http.conn = connData;

os_printf("sscp: %d handling %s request\n", connection->hdr.index, connData->url);
        
    return HTTPD_CGI_MORE;
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
os_printf("  captured %d bytes\n", count);
    
    char sendBuff[MAX_SENDBUFF_LEN];
    httpdSetSendBuffer(connData, sendBuff, sizeof(sendBuff));
    httpdSend(connData, connection->txBuffer, count);
    httpdFlushSendBuffer(connData);
    
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
