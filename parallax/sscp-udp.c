/*
    sscp-udp.c - Simple Serial Command Protocol UDP support

	Copyright (c) 2019 Parallax Inc.
    See the file LICENSE.txt for licensing information.
*/

#include "esp8266.h"
#include "sscp.h"
#include "config.h"

static void dns_cb(const char *name, ip_addr_t *ipaddr, void *arg);
static void udp_recv_cb(void *arg, char *data, unsigned short len);
static void udp_sent_cb(void *arg);

static void send_data_event(sscp_connection *connection, int prefix);
static int checkForEvents_handler(sscp_hdr *hdr);
static void send_handler(sscp_hdr *hdr, int size);
static void recv_handler(sscp_hdr *hdr, int size);
static void close_handler(sscp_hdr *hdr);

static sscp_dispatch udpDispatch = {
    .checkForEvents = checkForEvents_handler,
    .path = NULL,
    .send = send_handler,
    .recv = recv_handler,
    .close = close_handler
};

void ICACHE_FLASH_ATTR udp_do_connect(int argc, char *argv[])
{
	sscp_connection *c;
	struct espconn *conn;
	ip_addr_t ipAddr;

	if (argc != 3) {
		sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
		return;
	}

	// allocate a connection
	if (!(c = sscp_allocate_connection(TYPE_UDP_CONNECTION, &udpDispatch))) {
		sscp_sendResponse("E,%d", SSCP_ERROR_NO_FREE_CONNECTION);
		return;
	}

	conn = &c->d.udp.conn;
	os_memset(&c->d.udp, 0, sizeof(c->d.udp));
	conn->type = ESPCONN_UDP;
	conn->state = ESPCONN_NONE;
	conn->proto.udp = &c->d.udp.udp;
	conn->proto.udp->remote_port = atoi(argv[2]);
	if (conn->proto.udp->remote_port > 1023) {
 	   conn->proto.udp->local_port = conn->proto.udp->remote_port;
        }
	conn->reverse = (void *)c;

	espconn_regist_recvcb(conn, udp_recv_cb);
	espconn_regist_sentcb(conn, udp_sent_cb);
	
	if (isdigit((int)*argv[1]))
	   ipAddr.addr = ipaddr_addr(argv[1]);
	else {
		switch (espconn_gethostbyname(conn, argv[1], &ipAddr, dns_cb)) {
		case ESPCONN_OK:
			// connect below
			break;
		case ESPCONN_INPROGRESS:
			// response is sent by udp_connect_cb or udp_recon_cb
			sscp_log("UDP: looking up '%s'", argv[1]);
			return;
		default:
			sscp_close_connection(c);
			sscp_sendResponse("E,%d", SSCP_ERROR_LOOKUP_FAILED);
			return;
		}
	}

	memcpy(conn->proto.udp->remote_ip, &ipAddr.addr, 4);

	// response is sent by udp_connect_cb or udp_recon_cb
	c->d.udp.state = TCP_STATE_CONNECTING;

	if (espconn_create(conn) != 0)
	{
		sscp_close_connection(c);
		sscp_sendResponse("E,%d", SSCP_ERROR_CONNECT_FAILED);
		return;
	}

	c->d.udp.state = TCP_STATE_CONNECTED;
	sscp_sendResponse("S,%d", c->hdr.handle);
}

static void ICACHE_FLASH_ATTR dns_cb(const char *name, ip_addr_t *ipaddr, void *arg)
{
	struct espconn *conn = (struct espconn *)arg;
	sscp_connection *c = (sscp_connection *)conn->reverse;

	if (!ipaddr) {
		sscp_log("UDP: no IP address found for '%s'", name);
		sscp_close_connection(c);
		sscp_sendResponse("E,%d", SSCP_ERROR_LOOKUP_FAILED);
		return;
	}

	sscp_log("UDP: found IP address %d.%d.%d.%d for '%s'",
		*((uint8 *)&ipaddr->addr),
		*((uint8 *)&ipaddr->addr + 1),
		*((uint8 *)&ipaddr->addr + 2),
		*((uint8 *)&ipaddr->addr + 3),
		name);

	os_memcpy(conn->proto.udp->remote_ip, &ipaddr->addr, 4);

	if (espconn_create(conn) != 0) {
		sscp_close_connection(c);
		sscp_sendResponse("E,%d", SSCP_ERROR_CONNECT_FAILED);
		return;
	}

	c->d.udp.state = TCP_STATE_CONNECTED;
	sscp_sendResponse("S,%d", c->hdr.handle);
}

static void ICACHE_FLASH_ATTR udp_recv_cb(void *arg, char *data, unsigned short len)
{
	struct espconn *conn = (struct espconn *)arg;
	sscp_connection *c = (sscp_connection *)conn->reverse;
	sscp_log("UDP Handle: %d received %d bytes", c->hdr.handle, len);
	if (!(c->flags & CONNECTION_RXFULL)) {
		if (len > SSCP_RX_BUFFER_MAX)
                   len = SSCP_RX_BUFFER_MAX;
		os_memcpy(c->rxBuffer, data, len);
		c->rxCount = len;
		c->rxIndex = 0;
		c->flags |= CONNECTION_RXFULL;
		if (flashConfig.sscp_events)
			send_data_event(c, '!');
	}
}

static void ICACHE_FLASH_ATTR udp_sent_cb(void *arg)
{
	struct espconn *conn = (struct espconn *)arg;
	sscp_connection *c = (sscp_connection *)conn->reverse;
	c->flags &= ~CONNECTION_TXFULL;
	c->flags |= CONNECTION_TXDONE;
	sscp_log("UDP Handle: %d sent %d bytes", c->hdr.handle, c->rxCount);
	sscp_sendResponse("S,0");
}

static void ICACHE_FLASH_ATTR send_cb(void *data, int count)
{
	sscp_connection *c = (sscp_connection *)data;
	struct espconn *conn = &c->d.udp.conn;
	conn->state = ESPCONN_NONE;
	if (espconn_sendto(conn, (uint8 *)c->txBuffer, count) != ESPCONN_OK) {
		c->flags &= ~CONNECTION_TXFULL;
		sscp_sendResponse("E,%d", SSCP_ERROR_SEND_FAILED);
	}
}

static void ICACHE_FLASH_ATTR send_handler(sscp_hdr *hdr, int size)
{
	sscp_connection *c = (sscp_connection *)hdr;
	if (c->d.udp.state != TCP_STATE_CONNECTED) {
		sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
		return;
	}

	if (size == 0)
		sscp_sendResponse("S,0");
	else {
		// response is sent by udp_sent_cb
		sscp_capturePayload(c->txBuffer, size, send_cb, c);
		c->flags |= CONNECTION_TXFULL;
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

static void ICACHE_FLASH_ATTR send_data_event(sscp_connection *connection, int prefix)
{
	sscp_sendResponse("D,%d,%d", connection->hdr.handle, connection->rxCount);
}

static int ICACHE_FLASH_ATTR checkForEvents_handler(sscp_hdr *hdr)
{
	sscp_connection *connection = (sscp_connection *)hdr;

	if (connection->flags & CONNECTION_RXFULL) {
		send_data_event(connection, '=');
		return 1;
	}

	return 0;
}

static void ICACHE_FLASH_ATTR close_handler(sscp_hdr *hdr)
{
	sscp_connection *connection = (sscp_connection *)hdr;
	struct espconn *conn = &connection->d.udp.conn;
	if (conn)
	{
		espconn_delete(conn);
	}
}
