#include "esp8266.h"
#include "sscp.h"
#include "httpd.h"
#include "uart.h"
#include "config.h"
#include "cgiwifi.h"

static sscp_listener tcp_listener = {
    .type = LISTENER_TCP
};

static void ICACHE_FLASH_ATTR tcp_recv_cb(void *arg, char *pdata, unsigned short len)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;
    ++c;
}

static void ICACHE_FLASH_ATTR tcp_sent_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;
    ++c;
}

static void ICACHE_FLASH_ATTR tcp_discon_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;
    c->d.tcp.state = TCP_STATE_IDLE;
}

static void ICACHE_FLASH_ATTR tcp_connect_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;

    espconn_regist_disconcb(conn, tcp_discon_cb);
    espconn_regist_recvcb(conn, tcp_recv_cb);
    espconn_regist_sentcb(conn, tcp_sent_cb);

    c->d.tcp.state = TCP_STATE_CONNECTED;
    sscp_sendResponse("S,%d", c->index);
}

static void ICACHE_FLASH_ATTR tcp_recon_cb(void *arg, sint8 errType)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;

    c->d.tcp.state = TCP_STATE_IDLE;
    sscp_sendResponse("ERROR");
}

void ICACHE_FLASH_ATTR tcp_do_connect(int argc, char *argv[])
{
    sscp_connection *c;
    struct espconn *conn;
    uint32 ipAddr;

    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    // allocate a connection
    if (!(c = sscp_allocate_connection(&tcp_listener))) {
        sscp_sendResponse("E,0");
        return;
    }
    conn = &c->d.tcp.conn;

    os_memset(&c->d.tcp.conn, 0, sizeof(struct HttpdConnData));
    conn->type = ESPCONN_UDP;
    conn->state = ESPCONN_NONE;
    conn->proto.tcp = &c->d.tcp.tcp;
    ipAddr = ipaddr_addr(argv[1]);
    memcpy(conn->proto.tcp->remote_ip, &ipAddr, sizeof(ipAddr));
    conn->proto.tcp->remote_port = atoi(argv[2]);
    conn->reverse = (void *)c;

    espconn_regist_connectcb(conn, tcp_connect_cb);
    espconn_regist_reconcb(conn, tcp_recon_cb);

    if (espconn_connect(conn) != 0) {
        sscp_sendResponse("E,0");
        return;
    }

    // response sent by tcp_connect_cb or tcp_recon_cb
    c->d.tcp.state = TCP_STATE_CONNECTING;
}

void ICACHE_FLASH_ATTR tcp_do_disconnect(int argc, char *argv[])
{
    sscp_connection *c;
    struct espconn *conn;

    if (argc != 2) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!(c = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (!c->listener || c->listener->type != LISTENER_TCP) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    conn = &c->d.tcp.conn;

    if (c->d.tcp.state != TCP_STATE_CONNECTED && c->d.tcp.state != TCP_STATE_CONNECTING) {
        sscp_sendResponse("ERROR");
        return;
    }
    espconn_disconnect(conn);
    c->d.tcp.state = TCP_STATE_IDLE;
    sscp_sendResponse("OK");
}

void ICACHE_FLASH_ATTR tcp_do_send(int argc, char *argv[])
{
    sscp_connection *c;
//    struct espconn *conn;

    if (argc != 3) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!(c = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (!c->listener || c->listener->type != LISTENER_TCP) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (c->d.tcp.state != TCP_STATE_CONNECTED) {
        sscp_sendResponse("ERROR");
        return;
    }
    
//    conn = &c->d.tcp.conn;

    c->flags |= CONNECTION_TXFULL;
}

void ICACHE_FLASH_ATTR tcp_do_recv(int argc, char *argv[])
{
    sscp_connection *c;

    if (argc != 2) {
        sscp_sendResponse("ERROR");
        return;
    }
    
    if (!(c = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (!c->listener || c->listener->type != LISTENER_TCP) {
        sscp_sendResponse("ERROR");
        return;
    }

    if (c->d.tcp.state != TCP_STATE_CONNECTED) {
        sscp_sendResponse("ERROR");
        return;
    }
}

