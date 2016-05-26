#include "esp8266.h"
#include "sscp.h"
#include "httpd.h"

// LISTEN,chan
void ICACHE_FLASH_ATTR http_do_listen(int argc, char *argv[])
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
    listener->type = LISTENER_HTTP;
    
    sscp_sendResponse("OK");
}

// ARG,chan
void ICACHE_FLASH_ATTR http_do_arg(int argc, char *argv[])
{
    char buf[128];
    sscp_connection *connection;
    HttpdConnData *connData;
    
    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!(connData = (HttpdConnData *)connection->d.http.conn) || connData->conn == NULL) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (httpdFindArg(connData->getArgs, argv[2], buf, sizeof(buf)) == -1) {
        sscp_sendResponse("ERROR");
        return;
    }

    sscp_sendResponse(buf);
}

// POSTARG,chan
void ICACHE_FLASH_ATTR http_do_postarg(int argc, char *argv[])
{
    char buf[128];
    sscp_connection *connection;
    HttpdConnData *connData;
    
    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!(connData = (HttpdConnData *)connection->d.http.conn) || connData->conn == NULL) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!connData->post->buff) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (httpdFindArg(connData->post->buff, argv[2], buf, sizeof(buf)) == -1) {
        sscp_sendResponse("ERROR");
        return;
    }

    sscp_sendResponse(buf);
}

#define MAX_SENDBUFF_LEN 1024

// REPLY,chan,code,payload
void ICACHE_FLASH_ATTR http_do_reply(int argc, char *argv[])
{
    sscp_connection *connection;
    HttpdConnData *connData;

    if (argc != 4) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (!connection->listener || connection->listener->type != LISTENER_HTTP) {
        sscp_sendResponse("ERROR");
        return;
    }
        
    if (!(connData = (HttpdConnData *)connection->d.http.conn) || connData->conn == NULL) {
        sscp_sendResponse("ERROR");
        return;
    }

    char sendBuff[MAX_SENDBUFF_LEN];
    httpdSetSendBuffer(connData, sendBuff, sizeof(sendBuff));
    
    char buf[20];
    int len = os_strlen(argv[3]);
    os_sprintf(buf, "%d", len);

    httpdStartResponse(connData, atoi(argv[2]));
    httpdHeader(connData, "Content-Length", buf);
    httpdEndHeaders(connData);
    httpdSend(connData, argv[3], len);
    httpdFlushSendBuffer(connData);
    
    sscp_remove_connection(connection);
    connData->cgi = NULL;

    sscp_sendResponse("OK");
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
