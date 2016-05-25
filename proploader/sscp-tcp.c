#include "esp8266.h"
#include "sscp.h"
#include "httpd.h"
#include "uart.h"
#include "config.h"
#include "cgiwifi.h"

typedef enum {
    TCP_STATE_IDLE = 0,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED
} tcp_state;

typedef enum {
    TCP_FLAG_SENDING = 0x00000001
} tcp_flag;

typedef struct {
    tcp_state state;
    struct espconn conn;
    esp_tcp tcp;
    int flags;
} tcp_connection;

static tcp_connection myConnection;

static void ICACHE_FLASH_ATTR tcp_recv_cb(void *arg, char *pdata, unsigned short len)
{
    struct espconn *conn = (struct espconn *)arg;
    tcp_connection *c = (tcp_connection *)conn->reverse;
    ++c;
}

static void ICACHE_FLASH_ATTR tcp_sent_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    tcp_connection *c = (tcp_connection *)conn->reverse;
    ++c;
}

static void ICACHE_FLASH_ATTR tcp_discon_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    tcp_connection *c = (tcp_connection *)conn->reverse;
    c->state = TCP_STATE_IDLE;
}

static void ICACHE_FLASH_ATTR tcp_connect_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    tcp_connection *c = (tcp_connection *)conn->reverse;

    espconn_regist_disconcb(conn, tcp_discon_cb);
    espconn_regist_recvcb(conn, tcp_recv_cb);
    espconn_regist_sentcb(conn, tcp_sent_cb);

    c->state = TCP_STATE_CONNECTED;
    sscp_sendResponse("OK");
}

static void ICACHE_FLASH_ATTR tcp_recon_cb(void *arg, sint8 errType)
{
    struct espconn *conn = (struct espconn *)arg;
    tcp_connection *c = (tcp_connection *)conn->reverse;

    c->state = TCP_STATE_IDLE;
    sscp_sendResponse("ERROR");
}

void ICACHE_FLASH_ATTR tcp_do_connect(int argc, char *argv[])
{
    tcp_connection *c = &myConnection;
    struct espconn *conn = &c->conn;
    uint32 ipAddr;

    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    os_memset(c, 0, sizeof(tcp_connection));
    conn->reverse = (void *)c;

    conn->type = ESPCONN_UDP;
    conn->state = ESPCONN_NONE;
    conn->proto.tcp = &c->tcp;
    ipAddr = ipaddr_addr(argv[1]);
    memcpy(conn->proto.tcp->remote_ip, &ipAddr, sizeof(ipAddr));
    conn->proto.tcp->remote_port = atoi(argv[2]);

    espconn_regist_connectcb(conn, tcp_connect_cb);
    espconn_regist_reconcb(conn, tcp_recon_cb);

    if (espconn_connect(conn) != 0) {
        sscp_sendResponse("ERROR");
        return;
    }

    // response sent by tcp_connect_cb or tcp_recon_cb
    c->state = TCP_STATE_CONNECTING;
}

void ICACHE_FLASH_ATTR tcp_do_disconnect(int argc, char *argv[])
{
    tcp_connection *c = &myConnection;
    struct espconn *conn = &c->conn;
    if (c->state != TCP_STATE_CONNECTED && c->state != TCP_STATE_CONNECTING) {
        sscp_sendResponse("ERROR");
        return;
    }
    espconn_disconnect(conn);
    c->state = TCP_STATE_IDLE;
    sscp_sendResponse("OK");
}

void ICACHE_FLASH_ATTR tcp_do_send(int argc, char *argv[])
{
    tcp_connection *c = &myConnection;
//    struct espconn *conn = &c->conn;
    if (c->state != TCP_STATE_CONNECTED) {
        sscp_sendResponse("ERROR");
        return;
    }
    c->flags |= TCP_FLAG_SENDING;
}

void ICACHE_FLASH_ATTR tcp_do_recv(int argc, char *argv[])
{
    tcp_connection *c = &myConnection;
//    struct espconn *conn = &c->conn;
    if (c->state != TCP_STATE_CONNECTED) {
        sscp_sendResponse("ERROR");
        return;
    }
}

