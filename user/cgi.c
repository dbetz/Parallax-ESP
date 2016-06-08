/*
Some random cgi routines. Used in the LED example and the page that returns the entire
flash as a binary. Also handles the hit counter on the main page.
*/

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */


#include <esp8266.h>
#include "cgi.h"
#include "io.h"
#include "config.h"

//cause I can't be bothered to write an ioGetLed()
static char currLedState=0;

//Cgi that turns the LED on or off according to the 'led' param in the POST data
int ICACHE_FLASH_ATTR cgiLed(HttpdConnData *connData) {
	int len;
	char buff[1024];
	
	if (connData->conn==NULL) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

	len=httpdFindArg(connData->post->buff, "led", buff, sizeof(buff));
	if (len!=0) {
		currLedState=atoi(buff);
		ioLed(currLedState);
	}

	httpdRedirect(connData, "led.tpl");
	return HTTPD_CGI_DONE;
}



//Template code for the led page.
int ICACHE_FLASH_ATTR tplLed(HttpdConnData *connData, char *token, void **arg) {
	char buff[128];
	if (token==NULL) return HTTPD_CGI_DONE;

	os_strcpy(buff, "Unknown");
	if (os_strcmp(token, "ledstate")==0) {
		if (currLedState) {
			os_strcpy(buff, "on");
		} else {
			os_strcpy(buff, "off");
		}
	}
	httpdSend(connData, buff, -1);
	return HTTPD_CGI_DONE;
}

static long hitCounter=0;

//Template code for the counter on the index page.
int ICACHE_FLASH_ATTR tplCounter(HttpdConnData *connData, char *token, void **arg) {
	char buff[128];
	if (token==NULL) return HTTPD_CGI_DONE;

	if (os_strcmp(token, "counter")==0) {
		hitCounter++;
		os_sprintf(buff, "%ld", hitCounter);
	}
	else if (os_strcmp(token, "version")==0) {
		os_strcpy(buff, VERSION);
	}
	else if (os_strcmp(token, "module-name")==0) {
		os_strcpy(buff, flashConfig.module_name);
	}
	else if (os_strcmp(token, "sta-ipaddr") == 0) {
        struct ip_info info;
        if (!wifi_get_ip_info(STATION_IF, &info))
            os_memset(&info, 0, sizeof(info));
        os_sprintf(buff, "%d.%d.%d.%d",
            (info.ip.addr >> 0) & 0xff,
            (info.ip.addr >> 8) & 0xff, 
            (info.ip.addr >>16) & 0xff,
            (info.ip.addr >>24) & 0xff);
	}
	else if (os_strcmp(token, "softap-ipaddr") == 0) {
        struct ip_info info;
        if (!wifi_get_ip_info(SOFTAP_IF, &info))
            os_memset(&info, 0, sizeof(info));
        os_sprintf(buff, "%d.%d.%d.%d",
            (info.ip.addr >> 0) & 0xff,
            (info.ip.addr >> 8) & 0xff, 
            (info.ip.addr >>16) & 0xff,
            (info.ip.addr >>24) & 0xff);
	}
	else if (os_strcmp(token, "sta-macaddr") == 0) {
        uint8 addr[6];
        if (!wifi_get_macaddr(STATION_IF, addr))
            os_memset(&addr, 0, sizeof(addr));
        os_sprintf(buff, "%02x:%02x:%02x:%02x:%02x:%02x", 
            addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	}
	else if (os_strcmp(token, "softap-macaddr") == 0) {
        uint8 addr[6];
        if (!wifi_get_macaddr(SOFTAP_IF, addr))
            os_memset(&addr, 0, sizeof(addr));
        os_sprintf(buff, "%02x:%02x:%02x:%02x:%02x:%02x", 
            addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	}
	httpdSend(connData, buff, -1);
	return HTTPD_CGI_DONE;
}
