/* this code is derived from the code in cgiwifi.c that is part of libhttpd */

#include <esp8266.h>
#include "sscp.h"
#include "cgiwifi.h"
#include "config.h"

static int scanDone = 0;
static int scanCount;

static void ICACHE_FLASH_ATTR send_scan_complete_event(int prefix)
{
    sscp_send(prefix, "A,%d,0", scanCount);
}

static void ICACHE_FLASH_ATTR scan_complete(void *data, int count)
{
    os_printf("scan done callback, found %d\n", count);
    
    scanCount = count;

    if (flashConfig.sscp_events)
        send_scan_complete_event('!');
    else
        scanDone = 1;
}

int ICACHE_FLASH_ATTR wifi_check_for_events(void)
{
    int sentEvent = 0;
    if (scanDone) {
        send_scan_complete_event('=');
        scanDone = 0;
        sentEvent = 1;
    }
    return sentEvent;
 }

// APSCAN
void ICACHE_FLASH_ATTR wifi_do_apscan(int argc, char *argv[])
{
    if (argc != 1) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    scanDone = 0;
    
    if (cgiWiFiStartScan(scan_complete, NULL) == 1) {
        sscp_sendResponse("S,0");
    } else {
        sscp_sendResponse("E,%d", SSCP_ERROR_BUSY);
    }
}

// APGET:index
void ICACHE_FLASH_ATTR wifi_do_apget(int argc, char *argv[])
{
    int index;

    if (argc != 2) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }

    index = atoi(argv[1]);

    os_printf("ENTRY:%d\n", index);
    
    if (!cgiWiFiScanDone())
        sscp_sendResponse("N,0");
    else {
        ApData *entry = cgiWiFiScanResult(index);
        if (entry)
            //sscp_sendResponse("S,%d,%s,%d", entry->enc, entry->ssid, entry->rssi);  //Originally released with variable-length string as second-to-last field; inconsistent with rest of API
            sscp_sendResponse("S,%d,%d,%s", entry->enc, entry->rssi, entry->ssid);        
        else
            sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
    }
}



// APSET,ssid,passwd, optional flag for restart
void ICACHE_FLASH_ATTR wifi_do_apset(int argc, char *argv[])
{
    if (argc == 4) { // Special case- restart after command
        sscp_sendResponse("S,0");
        wifiSetCredentials(argv[1], argv[2], 1);
        return;
    }
    else if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    if (wifiSetCredentials(argv[1], argv[2], 0) == 0)
        sscp_sendResponse("S,0");
    else
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_ARGUMENT);
}



