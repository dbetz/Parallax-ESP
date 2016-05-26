#include "esp8266.h"
#include "sscp.h"

static sscp_listener tcp_listener = {
    .type = LISTENER_TCP
};

static void dns_cb(const char *name, ip_addr_t *ipaddr, void *arg);
static void tcp_connect_cb(void *arg);
static void tcp_recon_cb(void *arg, sint8 errType);

void ICACHE_FLASH_ATTR tcp_do_connect(int argc, char *argv[])
{
    sscp_connection *c;
    struct espconn *conn;
    ip_addr_t ipAddr;

    if (argc != 3) {
        sscp_sendResponse("E,0");
        return;
    }
    
    // allocate a connection
    if (!(c = sscp_allocate_connection(&tcp_listener))) {
        sscp_sendResponse("E,1");
        return;
    }
    conn = &c->d.tcp.conn;

    os_memset(&c->d.tcp.conn, 0, sizeof(struct HttpdConnData));
    conn->type = ESPCONN_TCP;
    conn->state = ESPCONN_NONE;
    conn->proto.tcp = &c->d.tcp.tcp;
    conn->proto.tcp->remote_port = atoi(argv[2]);
    conn->reverse = (void *)c;

    espconn_regist_connectcb(conn, tcp_connect_cb);
    espconn_regist_reconcb(conn, tcp_recon_cb);

    if (isdigit((int)*argv[1]))
        ipAddr.addr = ipaddr_addr(argv[1]);
    else {
        switch (espconn_gethostbyname(conn, argv[1], &ipAddr, dns_cb)) {
        case ESPCONN_OK:
            // connect below
            break;
        case ESPCONN_INPROGRESS:
            // response is sent by tcp_connect_cb or tcp_recon_cb
            os_printf("TCP: looking up '%s'\n", argv[1]);
            return;
        default:
            sscp_sendResponse("E,2");
            return;
        }
    }
    
    memcpy(conn->proto.tcp->remote_ip, &ipAddr.addr, 4);

    if (espconn_connect(conn) != ESPCONN_OK) {
        sscp_sendResponse("E,3");
        return;
    }

    // response is sent by tcp_connect_cb or tcp_recon_cb
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

static void send_cb(void *data)
{
    sscp_connection *c = (sscp_connection *)data;
    struct espconn *conn = &c->d.tcp.conn;
    if (espconn_send(conn, (uint8 *)c->txBuffer, c->txCount) != ESPCONN_OK) {
        c->flags &= ~CONNECTION_TXFULL;
        sscp_sendResponse("E,5");
    }
}

// TCP-SEND,chan,count
void ICACHE_FLASH_ATTR tcp_do_send(int argc, char *argv[])
{
    sscp_connection *c;
    int length;

    if (argc != 3) {
        sscp_sendResponse("E,0");
        return;
    }
    
    if (!(c = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("E,1");
        return;
    }

    if (!c->listener || c->listener->type != LISTENER_TCP) {
        sscp_sendResponse("E,2");
        return;
    }
    
    if (c->d.tcp.state != TCP_STATE_CONNECTED) {
        sscp_sendResponse("E,3");
        return;
    }
    
    if ((length = atoi(argv[2])) <= 0 || length > SSCP_TX_BUFFER_MAX) {
        sscp_sendResponse("E,4");
        return;
    }
    
    // response is sent by tcp_sent_cb
    c->txCount = length;
    sscp_capturePayload(c->txBuffer, length, send_cb, c);
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
    
    if (!(c->flags & CONNECTION_RXFULL)) {
        sscp_sendResponse("EMPTY");
        return;
    }

    sscp_sendResponse(c->rxBuffer);
    c->flags &= ~CONNECTION_RXFULL;
}

static void ICACHE_FLASH_ATTR tcp_recv_cb(void *arg, char *data, unsigned short len)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;
    if (!(c->flags & CONNECTION_RXFULL)) {
        if (len > SSCP_RX_BUFFER_MAX)
            len = SSCP_RX_BUFFER_MAX;
        os_memcpy(c->rxBuffer, data, len);
        c->rxCount = len;
        c->flags |= CONNECTION_RXFULL;
    }
}

static void ICACHE_FLASH_ATTR tcp_sent_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;
    c->flags &= ~CONNECTION_TXFULL;
    sscp_sendResponse("OK");
}

static void ICACHE_FLASH_ATTR tcp_discon_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;
    c->d.tcp.state = TCP_STATE_IDLE;
}

static void ICACHE_FLASH_ATTR dns_cb(const char *name, ip_addr_t *ipaddr, void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;

    if (!ipaddr) {
        os_printf("TCP: no IP address found for '%s'\n", name);
        sscp_sendResponse("E,4");
        return;
    }
    
    os_printf("TCP: found IP address %d.%d.%d.%d for '%s'\n",
                *((uint8 *)&ipaddr->addr),
                *((uint8 *)&ipaddr->addr + 1),
                *((uint8 *)&ipaddr->addr + 2),
                *((uint8 *)&ipaddr->addr + 3),
                name);
                
    os_memcpy(conn->proto.tcp->remote_ip, &ipaddr->addr, 4);
    
    if (espconn_connect(conn) != ESPCONN_OK) {
        sscp_free_connection(c);
        sscp_sendResponse("E,5");
    }
    
    // response is sent by tcp_connect_cb or tcp_recon_cb
    c->d.tcp.state = TCP_STATE_CONNECTING;
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
    sscp_sendResponse("E,6");
}

