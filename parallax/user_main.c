/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

/*
This is example code for the esphttpd library. It's a small-ish demo showing off 
the server, including WiFi connection management capabilities, some IO and
some pictures of cats.
*/

#include <esp8266.h>
#ifdef USE_AT
#include <at_custom.h>
#endif
#include "httpd.h"
#include "httpdespfs.h"
#include "cgiwifi.h"
#include "cgiflash.h"
#include "auth.h"
#include "espfs.h"
#include "captdns.h"
#include "webpages-espfs.h"
#include "cgiwebsocket.h"

#include "serbridge.h"
#include "uart.h"
#include "config.h"
#include "status.h"
#include "log.h"

#define PROPLOADER

#ifdef PROPLOADER
#include "cgiprop.h"
#include "httpdroffs.h"
#include "discovery.h"
#include "sscp.h"
#endif

//The example can print out the heap use every 3 seconds. You can use this to catch memory leaks.
//#define SHOW_HEAP_USE
#define SHOW_HEAP_INTERVAL  3000

//Test WebSockets broadcast messages
//#define TEST_BROADCAST

//Function that tells the authentication system what users/passwords live on the system.
//This is disabled in the default build; if you want to try it, enable the authBasic line in
//the builtInUrls below.
int ICACHE_FLASH_ATTR myPassFn(HttpdConnData *connData, int no, char *user, int userLen, char *pass, int passLen) {
	if (no==0) {
		os_strcpy(user, "admin");
		os_strcpy(pass, "s3cr3t");
		return 1;
//Add more users this way. Check against incrementing no for each user added.
//	} else if (no==1) {
//		os_strcpy(user, "user1");
//		os_strcpy(pass, "something");
//		return 1;
	}
	return 0;
}

#ifdef TEST_BROADCAST

static ETSTimer websockTimer;

//Broadcast the uptime in seconds every second over connected websockets
static void ICACHE_FLASH_ATTR websockTimerCb(void *arg) {
	static int ctr=0;
	char buff[128];
	ctr++;
	os_sprintf(buff, "Up for %d minutes %d seconds!\n", ctr/60, ctr%60);
	cgiWebsockBroadcast("/websocket/ws.cgi", buff, os_strlen(buff), WEBSOCK_FLAG_NONE);
}

#endif

//On reception of a message, send "You sent: " plus whatever the other side sent
void ICACHE_FLASH_ATTR myWebsocketRecv(Websock *ws, char *data, int len, int flags) {
	int i;
	char buff[128];
	os_sprintf(buff, "You sent: ");
	for (i=0; i<len; i++) buff[i+10]=data[i];
	buff[i+10]=0;
	cgiWebsocketSend(ws, buff, os_strlen(buff), WEBSOCK_FLAG_NONE);
}

//Websocket connected. Install reception handler and send welcome message.
void ICACHE_FLASH_ATTR myWebsocketConnect(Websock *ws) {
	ws->recvCb=myWebsocketRecv;
	cgiWebsocketSend(ws, "Hi, Websocket!", 14, WEBSOCK_FLAG_NONE);
}

//On reception of a message, echo it back verbatim
void ICACHE_FLASH_ATTR myEchoWebsocketRecv(Websock *ws, char *data, int len, int flags) {
	os_printf("EchoWs: echo, len=%d\n", len);
	cgiWebsocketSend(ws, data, len, flags);
}

//Echo websocket connected. Install reception handler.
void ICACHE_FLASH_ATTR myEchoWebsocketConnect(Websock *ws) {
	os_printf("EchoWs: connect\n");
	ws->recvCb=myEchoWebsocketRecv;
}


#ifdef ESPFS_POS
CgiUploadFlashDef uploadParams={
	.type=CGIFLASH_TYPE_ESPFS,
	.fw1Pos=ESPFS_POS,
	.fw2Pos=0,
	.fwSize=ESPFS_SIZE,
};
#define INCLUDE_FLASH_FNS
#endif
#ifdef OTA_FLASH_SIZE_K
CgiUploadFlashDef uploadParams={
	.type=CGIFLASH_TYPE_FW,
	.fw1Pos=0x1000,
	.fw2Pos=((OTA_FLASH_SIZE_K*1024)/2)+0x1000,
	.fwSize=((OTA_FLASH_SIZE_K*1024)/2)-0x1000,
	.tagName=OTA_TAGNAME
};
#define INCLUDE_FLASH_FNS
#endif

static int ICACHE_FLASH_ATTR cgiGetFirmwareNextFilter(HttpdConnData *connData) {
#ifdef AUTO_LOAD
    if (connData->conn != NULL && IsAutoLoadEnabled()) {
        httpdSendResponse(connData, 400, "Not allowed\r\n", -1);
        return HTTPD_CGI_DONE;
    }
#endif
    return cgiGetFirmwareNext(connData);
}

int ICACHE_FLASH_ATTR cgiUploadFirmwareFilter(HttpdConnData *connData) {
#ifdef AUTO_LOAD
    if (connData->conn != NULL && IsAutoLoadEnabled()) {
        httpdSendResponse(connData, 400, "Not allowed\r\n", -1);
        return HTTPD_CGI_DONE;
    }
#endif
    return cgiUploadFirmware(connData);
}

int ICACHE_FLASH_ATTR cgiWiFiConnectFilter(HttpdConnData *connData) {
#ifdef AUTO_LOAD
    if (connData->conn != NULL && IsAutoLoadEnabled()) {
        httpdSendResponse(connData, 400, "Not allowed\r\n", -1);
        return HTTPD_CGI_DONE;
    }
#endif
    return cgiWiFiConnect(connData);
}

int ICACHE_FLASH_ATTR cgiWiFiSetModeFilter(HttpdConnData *connData) {
#ifdef AUTO_LOAD
    if (connData->conn != NULL && IsAutoLoadEnabled()) {
        httpdSendResponse(connData, 400, "Not allowed\r\n", -1);
        return HTTPD_CGI_DONE;
    }
#endif
    return cgiWiFiSetMode(connData);
}

/*
This is the main url->function dispatching data struct.
In short, it's a struct with various URLs plus their handlers. The handlers can
be 'standard' CGI functions you wrote, or 'special' CGIs requiring an argument.
They can also be auth-functions. An asterisk will match any url starting with
everything before the asterisks; "*" matches everything. The list will be
handled top-down, so make sure to put more specific rules above the more
general ones. Authorization things (like authBasic) act as a 'barrier' and
should be placed above the URLs they protect.
*/
HttpdBuiltInUrl builtInUrls[]={
	{"*", cgiRedirectApClientToHostname, "esp8266.nonet"},
	{"/", cgiRedirect, "/index.html"},
#ifdef INCLUDE_FLASH_FNS
	{"/flash/next", cgiGetFirmwareNextFilter, &uploadParams},
	{"/flash/upload", cgiUploadFirmwareFilter, &uploadParams},
#endif
	{"/flash/reboot", cgiRebootFirmware, NULL},

	{"/websocket", cgiRedirect, "/websocket/index.html"},
	{"/websocket/", cgiRedirect, "/websocket/index.html"},
	{"/websocket/ws.cgi", cgiWebsocket, myWebsocketConnect},
	{"/websocket/echo.cgi", cgiWebsocket, myEchoWebsocketConnect},

	//Routines to make the /wifi URL and everything beneath it work.

//Enable the line below to protect the WiFi configuration with an username/password combo.
//	{"/wifi/*", authBasic, myPassFn},

	{"/wifi", cgiRedirect, "/wifi/wifi.html"},
	{"/wifi/", cgiRedirect, "/wifi/wifi.html"},
	{"/wifi/wifiscan.cgi", cgiWiFiScan, NULL},
	{"/wifi/connect.cgi", cgiWiFiConnectFilter, NULL},
	{"/wifi/connstatus.cgi", cgiWiFiConnStatus, NULL},
	{"/wifi/setmode.cgi", cgiWiFiSetModeFilter, NULL},

    {"/log/text", ajaxLog, NULL },

#ifdef PROPLOADER
    { "/userfs/format", cgiRoffsFormat, NULL },
    { "/userfs/write", cgiRoffsWriteFile, NULL },
    { "/propeller/load", cgiPropLoad, NULL },
    { "/propeller/load-file", cgiPropLoadFile, NULL },
    { "/propeller/reset", cgiPropReset, NULL },
    { "/wx/module-info", cgiPropModuleInfo, NULL },
    { "/wx/setting", cgiPropSetting, NULL },
    { "/wx/save-settings", cgiPropSaveSettings, NULL },
    { "/wx/restore-settings", cgiPropRestoreSettings, NULL },
    { "/wx/restore-default-settings", cgiPropRestoreDefaultSettings, NULL },
    { "/files/*", cgiRoffsHook, NULL }, //Catch-all cgi function for the flash filesystem
	{ "/ws/*", cgiWebsocket, sscp_websocketConnect},
    { "*", cgiSSCPHandleRequest, NULL }, //Check to see if MCU can handle the request
#endif

	{"*", cgiEspFsHook, NULL}, //Catch-all cgi function for the filesystem
	{NULL, NULL, NULL}
};


#ifdef SHOW_HEAP_USE
static ETSTimer prHeapTimer;

static void ICACHE_FLASH_ATTR prHeapTimerCb(void *arg) {
	os_printf("Heap: %ld\n", (unsigned long)system_get_free_heap_size());
}
#endif

//Main routine. Initialize stdout, the I/O, filesystem and the webserver and we're done.
void ICACHE_FLASH_ATTR user_init(void) {
    int restoreOk;
    
    //wifi_station_set_auto_connect(TRUE); // Default on; may be overwritten by valid flash config

    if (!(restoreOk = configRestore()))
        //wifi_station_set_auto_connect(TRUE); // Default on; may be overwritten by valid flash config
        configSave();

    wifi_station_set_hostname(flashConfig.module_name);

    captdnsInit();

    // init UART
    uart_init(flashConfig.baud_rate, 115200);
    logInit();

    os_printf("Flash config restore %s\n", restoreOk ? "ok" : "*FAILED*");
    os_printf("Reset Pin: %d\n", flashConfig.reset_pin);
    os_printf("RX Pullup: %d\n", flashConfig.rx_pullup);

    statusInit();

    // init the wifi-serial transparent bridge (port 23)
    serbridgeInit(23);
    uart_add_recv_cb(&serbridgeUartCb);

#ifdef PROPLOADER
    initDiscovery();
    cgiPropInit();
    sscp_init();
#endif

	// 0x40200000 is the base address for spi flash memory mapping, ESPFS_POS is the position
	// where image is written in flash that is defined in Makefile.
#ifdef ESPFS_POS
	espFsInit((void*)(0x40200000 + ESPFS_POS));
#else
	espFsInit((void*)(webpages_espfs_start));
#endif
	httpdInit(builtInUrls, 80);
#ifdef SHOW_HEAP_USE
	os_timer_disarm(&prHeapTimer);
	os_timer_setfn(&prHeapTimer, prHeapTimerCb, NULL);
	os_timer_arm(&prHeapTimer, SHOW_HEAP_INTERVAL, 1);
#endif
#ifdef TEST_BROADCAST
	os_timer_disarm(&websockTimer);
	os_timer_setfn(&websockTimer, websockTimerCb, NULL);
	os_timer_arm(&websockTimer, 10000, 1);
#endif
	os_printf("\nReady\n");
}

void ICACHE_FLASH_ATTR user_rf_pre_init() {
	//Not needed, but some SDK versions want this defined.
}

uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void)
{
    enum flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 8;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;

        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

#if defined(ESP_SDK_VERSION) && ESP_SDK_VERSION >= 030000
// Add default function from ESP docs during V3 testing. 
// TODO: Values/Addresses defined in this function may need adjusting

// user_pre_init is required from SDK v3.0.0 onwards
// It is used to register the parition map with the SDK, primarily to allow
// the app to use the SDK's OTA capability.  We don't make use of that in 
// otb-iot and therefore the only info we provide is the mandatory stuff:
// - RF calibration data
// - Physical data
// - System parameter
// The location and length of these are from the 2A SDK getting started guide
void ICACHE_FLASH_ATTR user_pre_init(void)
{
  bool rc = false;
  static const partition_item_t part_table[] = 
  {
    {SYSTEM_PARTITION_RF_CAL,
     0x3fb000,
     0x1000},
    {SYSTEM_PARTITION_PHY_DATA,
     0x3fc000,
     0x1000},
    {SYSTEM_PARTITION_SYSTEM_PARAMETER,
     0x3fd000,
     0x3000},
  };

  // This isn't an ideal approach but there's not much point moving on unless
  // or until this has succeeded cos otherwise the SDK will just barf and 
  // refuse to call user_init()
  while (!rc)
  {
    rc = system_partition_table_regist(part_table, sizeof(part_table)/sizeof(part_table[0]), 4);
  }

  return;
}
#endif // ESP_SDK_VERSION >= 030000


