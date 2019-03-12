/*
    sscp-cmds.c - Simple Serial Command Protocol commands

	Copyright (c) 2016 Parallax Inc.
    See the file LICENSE.txt for licensing information.
*/

#include "esp8266.h"
#include "sscp.h"
#include "uart.h"
#include "config.h"
#include "cgiprop.h"
#include "cgiwifi.h"
#include "user_interface.h"


static int LockState = 0; // default state goes by the autoload pin

// (nothing)
void ICACHE_FLASH_ATTR cmds_do_nothing(int argc, char *argv[])
{
    sscp_sendResponse("S,0");
}

int ICACHE_FLASH_ATTR cmds_check_lockstate(void) {
    
    return LockState;
    
}

// JOIN,ssid,passwd
void ICACHE_FLASH_ATTR cmds_do_join(int argc, char *argv[])
{
    if (argc == 1) {
        
        // Auto Join!
        wifiJoinAuto();
        sscp_sendResponse("S,0");
        return;
    }
    
    else if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (wifiJoin(argv[1], argv[2]) == 0)
        sscp_sendResponse("S,0");
    else
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT); // mm: This can never be called ???!! wifiJoin can only return 0 as currently coded
}

static void ICACHE_FLASH_ATTR path_handler(sscp_hdr *hdr)
{
    sscp_listener *listener = (sscp_listener *)hdr;
    sscp_sendResponse("S,%s", listener->path);
}

static sscp_dispatch listenerDispatch = {
    .checkForEvents = NULL,
    .path = path_handler,
    .send = NULL,
    .recv = NULL,
    .close = NULL
};

// LISTEN,proto,chan
void ICACHE_FLASH_ATTR cmds_do_listen(int argc, char *argv[])
{
    sscp_listener *listener;
    char *proto;
    int type;
    
    if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    proto = argv[1];
    
    if (os_strcmp(proto, "HTTP") == 0)
        type = TYPE_HTTP_LISTENER;
    
    else if (os_strcmp(proto, "WS") == 0)
        type = TYPE_WEBSOCKET_LISTENER;
    
    else if (os_strcmp(proto, "TCP") == 0)
        type = TYPE_TCP_LISTENER;
    
    else {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }

    if (os_strlen(argv[2]) >= SSCP_PATH_MAX) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_SIZE);
        return;
    }

    if (!(listener = sscp_allocate_listener(type, argv[2], &listenerDispatch))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_NO_FREE_LISTENER);
        return;
    }

    sscp_log("Listening for '%s' on %d", argv[2], listener->hdr.handle);
    
    sscp_sendResponse("S,%d", listener->hdr.handle);
}

// POLL
void ICACHE_FLASH_ATTR cmds_do_poll(int argc, char *argv[])
{
    uint32_t mask;
    int i;

    if (argc < 1 && argc > 2) {
        sscp_sendResponse("E,%d,0", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }

    mask = (argc > 1 ? atoi(argv[1]) : 0xffffffff);

    for (i = 0; i < SSCP_CONNECTION_MAX; ++i) {
        sscp_hdr *hdr = (sscp_hdr *)&sscp_connections[i];
        if ((1 << hdr->handle) & mask) {
            if (hdr->type != TYPE_UNUSED) {
                if (hdr->dispatch->checkForEvents && (*hdr->dispatch->checkForEvents)(hdr))
                    return;
            }
        }
    }

    if (wifi_check_for_events())
        return;
    
    sscp_sendResponse("N,0,0");
}

// PATH,chan
void ICACHE_FLASH_ATTR cmds_do_path(int argc, char *argv[])
{
    sscp_hdr *hdr;

    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (!(hdr = sscp_get_handle(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }

    if (hdr->dispatch->path)
        (*hdr->dispatch->path)(hdr);
    else
        sscp_sendResponse("E,%d", SSCP_ERROR_UNIMPLEMENTED);
}

// CLOSE,chan
void ICACHE_FLASH_ATTR cmds_do_close(int argc, char *argv[])
{
    sscp_hdr *hdr;

    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (!(hdr = sscp_get_handle(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }

    if (hdr->dispatch->close)
        (*hdr->dispatch->close)(hdr);
    hdr->type = TYPE_UNUSED;
        
    sscp_sendResponse("S,0");
}

// RESTART,hard
void ICACHE_FLASH_ATTR cmds_do_restart(int argc, char *argv[])
{
    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (atoi(argv[1]) == 0) {
        sscp_log("RESTART 0 : system_restart");
        //ESP.restart();
        system_restart();
        sscp_sendResponse("S,0");
        return;
    
    } else if (atoi(argv[1]) == 1) {
        sscp_log("RESTART 1 : system_upgrade_reboot");
        //ESP.reset();
        system_upgrade_reboot();
        sscp_sendResponse("S,0");
        return;
    
    } else {
        
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
}



// SLEEP:x,y
// Where x is deep-sleep-set-option (0,1,2,4)
// y is sleep duration in uSecs (0 to 4294967295) ie. max uint32
// Note: 0 might get unlimited sleep- need to test in this firmware version!

void ICACHE_FLASH_ATTR cmds_do_sleep(int argc, char *argv[])
{
  
    if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    
    wifi_station_disconnect();
    wifi_set_opmode(NULL_MODE);
    
    int i = atoi(argv[1]);
    uint32 usecs = (uint32)strtoul(argv[2], NULL, 0);
    
    os_printf("Set Sleep Mode = %d, %d\n", i, usecs);
    
    if (i == 0 || i == 1 || i == 2 || i ==4) {
        
        sscp_sendResponse("S,0");
        
        //os_delay_us(1000000L); // 1 second
    
        system_deep_sleep_set_option((uint8)i); // 2 = Disable RF calibration at wake-up to save time and power.
        
        system_deep_sleep(usecs); // 4294967295);   // time in uSecs // strtoul will return 0 or maxlong if value invalid

        // To wake up WiFi, toggle reset pin low, then input (it has 10K pull-up to 3v3))
        // - When module wakes up, it resumes from the start! Beware of OLED clearing feature!

        
    
    } else {
        
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
    
    }
    
    
    return;
    
    
}



// LOCK:x
// x=0, neutral-default (can be overriden by lock state on auto-load pin)
// x=1, remove lock (overrides lock state on auto-load pin)
// x=2, force lock (overrides lock state on auto-load pin)

void ICACHE_FLASH_ATTR cmds_do_lock(int argc, char *argv[])
{
    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    int i = atoi(argv[1]);
    
    if ((i==0) || (i==1) || (i==2)) {
        
        os_printf("Set Lock Mode = %d\n", i);
        LockState = i;
        sscp_sendResponse("S,0");
        
    } else {
        
       sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT); 
       
    }
       
        
    return;

}



// SEND,chan,count
void ICACHE_FLASH_ATTR cmds_do_send(int argc, char *argv[])
{
    sscp_connection *connection;
    int count;

    if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
    
    if (connection->flags & CONNECTION_TXFULL) {
        sscp_sendResponse("E,%d", SSCP_ERROR_BUSY);
        return;
    }
    
    if ((count = atoi(argv[2])) < 0 || count > SSCP_TX_BUFFER_MAX) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_SIZE);
        return;
    }
sscp_log("SEND %d %d", connection->hdr.handle, count);
    
    if (connection->hdr.dispatch->send)
        (*connection->hdr.dispatch->send)((sscp_hdr *)connection, count);
    else
        sscp_sendResponse("E,%d", SSCP_ERROR_UNIMPLEMENTED);
}

// RECV,chan,count
void ICACHE_FLASH_ATTR cmds_do_recv(int argc, char *argv[])
{
    sscp_connection *connection;
    int count;

    if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }

    if (!(connection = sscp_get_connection(atoi(argv[1])))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
    
    if ((count = atoi(argv[2])) < 0) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
        return;
    }
sscp_log("RECV %d %d", connection->hdr.handle, count);
    
    if (connection->hdr.dispatch->recv)
        (*connection->hdr.dispatch->recv)((sscp_hdr *)connection, count);
    else
        sscp_sendResponse("E,%d", SSCP_ERROR_UNIMPLEMENTED);
}

int ICACHE_FLASH_ATTR cgiPropModuleInfo(HttpdConnData *connData)
{
    struct ip_info sta_info;
    struct ip_info softap_info;
    uint8 sta_addr[6];
    uint8 softap_addr[6];
    char buf[1024];

    if (!wifi_get_ip_info(STATION_IF, &sta_info))
        os_memset(&sta_info, 0, sizeof(sta_info));
    if (!wifi_get_macaddr(STATION_IF, sta_addr))
        os_memset(&sta_addr, 0, sizeof(sta_addr));

    if (!wifi_get_ip_info(SOFTAP_IF, &softap_info))
        os_memset(&softap_info, 0, sizeof(softap_info));
    if (!wifi_get_macaddr(SOFTAP_IF, softap_addr))
        os_memset(&softap_addr, 0, sizeof(softap_addr));

    os_sprintf(buf, "\
{\n\
  \"name\": \"%s\",\n\
  \"sta-ipaddr\": \"%d.%d.%d.%d\",\n\
  \"sta-macaddr\": \"%02x:%02x:%02x:%02x:%02x:%02x\",\n\
  \"softap-ipaddr\": \"%d.%d.%d.%d\",\n\
  \"softap-macaddr\": \"%02x:%02x:%02x:%02x:%02x:%02x\"\n\
}\n",
        flashConfig.module_name,
        (sta_info.ip.addr >> 0) & 0xff,
        (sta_info.ip.addr >> 8) & 0xff, 
        (sta_info.ip.addr >>16) & 0xff,
        (sta_info.ip.addr >>24) & 0xff,
        sta_addr[0], sta_addr[1], sta_addr[2], sta_addr[3], sta_addr[4], sta_addr[5],
        (softap_info.ip.addr >> 0) & 0xff,
        (softap_info.ip.addr >> 8) & 0xff, 
        (softap_info.ip.addr >>16) & 0xff,
        (softap_info.ip.addr >>24) & 0xff,
        softap_addr[0], softap_addr[1], softap_addr[2], softap_addr[3], softap_addr[4], softap_addr[5]);
        
    httpdStartResponse(connData, 200);
    httpdEndHeaders(connData);
    httpdSend(connData, buf, -1);
    return HTTPD_CGI_DONE;
}
