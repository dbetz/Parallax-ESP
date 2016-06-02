#include "esp8266.h"
#include "sscp.h"
#include "httpd.h"

// LISTEN,chan
void ICACHE_FLASH_ATTR http_do_listen(int argc, char *argv[])
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
    listener->type = LISTENER_HTTP;
    
    sscp_sendResponse("S,0");
}

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

    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
    
    if (!(connData = (HttpdConnData *)connection->d.http.conn) || connData->conn == NULL) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
        return;
    }
    
    if (httpdFindArg(connData->getArgs, argv[2], buf, sizeof(buf)) == -1) {
        sscp_sendResponse("N,0");
        return;
    }

    sscp_sendResponse("S,%s", buf);
}

// POSTARG,chan
void ICACHE_FLASH_ATTR http_do_postarg(int argc, char *argv[])
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

    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
    
    if (!(connData = (HttpdConnData *)connection->d.http.conn) || connData->conn == NULL) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
        return;
    }
    
    if (!connData->post->buff) {
        sscp_sendResponse("N,0");
        return;
    }
    
    if (httpdFindArg(connData->post->buff, argv[2], buf, sizeof(buf)) == -1) {
        sscp_sendResponse("N,0");
        return;
    }

    sscp_sendResponse("S,%s", buf);
}

// BODY,chan
void ICACHE_FLASH_ATTR http_do_body(int argc, char *argv[])
{
    sscp_connection *connection;
    HttpdConnData *connData;
    int count;

    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }

    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
    
    if (!(connData = (HttpdConnData *)connection->d.http.conn) || connData->conn == NULL) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
        return;
    }
    
    count = connData->post->buff ? connData->post->len : 0;
    
    sscp_sendResponse("S,%d", count);
    if (count > 0)
        sscp_sendPayload(connData->post->buff, count);
}

#define MAX_SENDBUFF_LEN 1024

static void ICACHE_FLASH_ATTR reply_cb(void *data)
{
    sscp_connection *connection = (sscp_connection *)data;
    HttpdConnData *connData = connection->d.http.conn;
    
    char sendBuff[MAX_SENDBUFF_LEN];
    httpdSetSendBuffer(connData, sendBuff, sizeof(sendBuff));
    
    char buf[20];
    os_sprintf(buf, "%d", connection->txCount);

    httpdStartResponse(connData, connection->d.http.code);
    httpdHeader(connData, "Content-Length", buf);
    httpdEndHeaders(connData);
    httpdSend(connData, connection->txBuffer, connection->txCount);
    httpdFlushSendBuffer(connData);
    
    sscp_remove_connection(connection);
    connData->cgi = NULL;

    sscp_sendResponse("S,0");
}

// REPLY,chan,code,payload
void ICACHE_FLASH_ATTR http_do_reply(int argc, char *argv[])
{
    sscp_connection *connection;
    HttpdConnData *connData;
    int length;

    if (argc != 4) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }

    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
        
    if (!(connData = (HttpdConnData *)connection->d.http.conn) || connData->conn == NULL) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
        return;
    }
    
    connection->d.http.code = atoi(argv[2]);

    if ((length = atoi(argv[3])) < 0 || length > SSCP_TX_BUFFER_MAX) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_SIZE);
        return;
    }
    
    // response is sent by reply_cb
    connection->txCount = length;
    sscp_capturePayload(connection->txBuffer, length, reply_cb, connection);
    connection->flags |= CONNECTION_TXFULL;
}

int ICACHE_FLASH_ATTR cgiSSCPHandleRequest(HttpdConnData *connData)
{
    sscp_listener *listener;
    sscp_connection *connection;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;
    
    // find a matching listener
    if (!(listener = sscp_find_listener(connData->url, LISTENER_HTTP)))
        return HTTPD_CGI_NOTFOUND;

    // allocate a connection
    if (!(connection = sscp_allocate_connection(listener))) {
        httpdStartResponse(connData, 400);
        httpdEndHeaders(connData);
os_printf("sscp: no connections available for %s request\n", connData->url);
        httpdSend(connData, "No connections available", -1);
        return HTTPD_CGI_DONE;
    }
    connection->d.http.conn = connData;

os_printf("sscp: handling %s request\n", connData->url);
        
    return HTTPD_CGI_MORE;
}

void ICACHE_FLASH_ATTR http_disconnect(sscp_connection *connection)
{
    HttpdConnData *connData = connection->d.http.conn;
    if (connData)
        connData->cgi = NULL;
    sscp_free_connection(connection);
}
