/*
    discovery.c - support for the Parallax WX module discovery protocol

	Copyright (c) 2016 Parallax Inc.
    See the file LICENSE.txt for licensing information.
*/

#include <esp8266.h>
#include "config.h"
#include "discovery.h"

#define DISCOVER_PORT   32420

static struct espconn discoverConn;
static esp_udp discoverUdp;

static void ICACHE_FLASH_ATTR sentCallback(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    int *pDone = (int *)conn->reverse;
    *pDone = 1;
//    os_printf("Send completed\n");
}

static void ICACHE_FLASH_ATTR discoverRecvCallback(void *arg, char *data, unsigned short len)
{
    struct espconn *conn = (struct espconn *)arg;
    remot_info *remoteInfo;
    struct espconn responseConn;
    esp_udp responseUdp;
    uint32_t *rxNext;
    uint32_t myAddress = (conn->proto.udp->local_ip[3] << 24)
                       | (conn->proto.udp->local_ip[2] << 16)
                       | (conn->proto.udp->local_ip[1] << 8)
                       |  conn->proto.udp->local_ip[0];
    struct ip_info info;
    uint8 macAddr[6];

#if 0
os_printf("DISCOVER: myAddress %d.%d.%d.%d\n", conn->proto.udp->local_ip[0],
                                               conn->proto.udp->local_ip[1],
                                               conn->proto.udp->local_ip[2],
                                               conn->proto.udp->local_ip[3]);
#endif

    // check to see if we already replied to this request
    rxNext = (uint32_t *)data;
    if (len > sizeof(uint32_t)) {
        if (*rxNext++ != 0) {
            os_printf("DISCOVER: received a bogus discover request\n");
            return;
        }
        len -= sizeof(uint32_t);
        while (len >= sizeof(uint32_t)) {
#if 0
os_printf("DISCOVER: Checking %d.%d.%d.%d\n", (int)( *rxNext        & 0xff),
                                              (int)((*rxNext >>  8) & 0xff),
                                              (int)((*rxNext >> 16) & 0xff),
                                              (int)((*rxNext >> 24) & 0xff));
#endif
            if (*rxNext++ == myAddress) {
//                os_printf("DISCOVER: already replied\n");
                return;
            }
            len -= sizeof(uint32_t);
        }
    }

    // insert a random delay to avoid udp packet collisions from multiple modules
    os_delay_us(10000 + (unsigned)os_random() % 50000);

    if (espconn_get_connection_info(conn, &remoteInfo, 0) != 0) {
        os_printf("DISCOVER: espconn_get_connection_info failed\n");
        return;
    }

    os_memset(&responseConn, 0, sizeof(responseConn));
    responseConn.type = ESPCONN_UDP;
    responseConn.state = ESPCONN_NONE;
    responseConn.proto.udp = &responseUdp;
    os_memcpy(responseConn.proto.udp->remote_ip, remoteInfo->remote_ip, 4);
    responseConn.proto.udp->remote_port = remoteInfo->remote_port;

    if (wifi_get_ip_info(STATION_IF, &info) && info.ip.addr == myAddress)
        wifi_get_macaddr(STATION_IF, macAddr);
    else if (wifi_get_ip_info(SOFTAP_IF, &info) && info.ip.addr == myAddress)
        wifi_get_macaddr(SOFTAP_IF, macAddr);
    else
        memset(macAddr, 0, sizeof(macAddr));

    if (espconn_create(&responseConn) == 0) {
        char buf[1024];
        volatile int done = 0;
        int retries = 100;
        int len = os_sprintf(buf,
            "{ "
              "\"name\": \"%s\", "
              "\"description\": \"%s\", "
              "\"reset pin\": \"%d\", "
              "\"rx pullup\": \"%s\", "
              "\"mac address\": \"%02x:%02x:%02x:%02x:%02x:%02x\""
            " }\n",
            flashConfig.module_name,
            flashConfig.module_descr,
            flashConfig.reset_pin,
            flashConfig.rx_pullup ? "enabled" : "disabled",
            macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
//        os_printf("Sending %d byte discover response: %s", len, buf);
        espconn_regist_sentcb(&responseConn, sentCallback);
        responseConn.reverse = (int *)&done;
        espconn_send(&responseConn, (uint8 *)buf, len);
//        os_printf("Waiting for send to complete\n");
        while (!done && --retries >= 0)
            os_delay_us(10000);
        if (!done)
            os_printf("DISCOVER: failed to send discovery response\n");
//        os_printf("Done waiting for send to complete\n");
        espconn_delete(&responseConn);
    }
}

void ICACHE_FLASH_ATTR initDiscovery(void)
{
    discoverConn.type = ESPCONN_UDP;
    discoverConn.state = ESPCONN_NONE;
    discoverUdp.local_port = DISCOVER_PORT;
    discoverConn.proto.udp = &discoverUdp;
    
    if (espconn_create(&discoverConn) != 0) {
        os_printf("DISCOVER: espconn_create failed\n");
        return;
    }

    if (espconn_regist_recvcb(&discoverConn, discoverRecvCallback) != 0) {
        os_printf("DISCOVER: espconn_regist_recvcb failed\n");
        return;
    }
        
    os_printf("DISCOVER: initialized\n");
}
