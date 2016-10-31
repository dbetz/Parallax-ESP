// Copyright 2015 by Thorsten von Eicken, see LICENSE.txt

#include <esp8266.h>
#include "config.h"
//#include "serled.h"
#include "cgiwifi.h"

#ifdef MQTT
#include "mqtt.h"
#include "mqtt_client.h"
extern MQTT_Client mqttClient;

//#define STATUS_LED_ACTIVE_LOW

//===== MQTT Status update

// Every minute...
#define MQTT_STATUS_INTERVAL (60*1000)

static ETSTimer mqttStatusTimer;

int ICACHE_FLASH_ATTR
mqttStatusMsg(char *buf) {
  sint8 rssi = wifi_station_get_rssi();
  if (rssi > 0) rssi = 0; // not connected or other error
  //os_printf("timer rssi=%d\n", rssi);

  // compose MQTT message
  return os_sprintf(buf,
    "{\"rssi\":%d, \"heap_free\":%ld}",
    rssi, (unsigned long)system_get_free_heap_size());
}

// Timer callback to send an RSSI update to a monitoring system
static void ICACHE_FLASH_ATTR mqttStatusCb(void *v) {
  if (!flashConfig.mqtt_status_enable || os_strlen(flashConfig.mqtt_status_topic) == 0 ||
    mqttClient.connState != MQTT_CONNECTED)
    return;

  char buf[128];
  mqttStatusMsg(buf);
  MQTT_Publish(&mqttClient, flashConfig.mqtt_status_topic, buf, os_strlen(buf), 1, 0);
}



#endif // MQTT

//===== "CONN" LED status indication

enum { wifiIsDisconnected, wifiIsConnected, wifiGotIP };
uint8_t wifiState = wifiIsDisconnected;

// in serbridge.c
void makeGpio(uint8_t pin);

static ETSTimer ledTimer;

// Set the LED on or off, respecting the defined polarity
static void ICACHE_FLASH_ATTR setLed(int on) {
  int8_t pin = flashConfig.conn_led_pin;
  if (pin < 0) return; // disabled
#ifdef STATUS_LED_ACTIVE_LOW
  // LED is active-low
  if (on) {
    gpio_output_set(0, (1<<pin), (1<<pin), 0);
  } else {
    gpio_output_set((1<<pin), 0, (1<<pin), 0);
  }
#else
  // LED is active-high
  if (on) {
    gpio_output_set((1<<pin), 0, (1<<pin), 0);
  } else {
    gpio_output_set(0, (1<<pin), (1<<pin), 0);
  }
#endif
}

/*

Five States of Wireless System

In the description, bold is the wireless mode, small text indicates connectivity or lack
thereof, and after hyphen (-) indicates LED behavior and the timing I used in the video
below.

STA (no IP address)               [Not wirelessly accessible]  - OFF constantly
STA (has IP address)              [Wirelessly accessible]      - OFF 4000 ms, ON 25 ms
AP (has IP address)               [Wirelessly accessible]*     - ON constantly
STA+AP (no IP on STA, IP on AP)   [Wirelessly accessible]*     - OFF 2000 ms, ON 2000 ms
STA+AP (has IP on STA and AP)     [Wirelessly accessible]*     - OFF 2000 ms, ON 25 ms, 
                                                                 OFF 150 ms, ON 2000 ms

*In AP mode, there is always an IP address associated with the module because it itself 
is serving as the gateway.

*/

static uint8_t ledPhase = 0;

// Timer callback to update the LED
static void ICACHE_FLASH_ATTR ledTimerCb(void *v) {
    int state = 0;
    int time = 1000;

    switch (wifi_get_opmode()) {
    case STATION_MODE:
        switch (ledPhase) {
        case 0:
        case 2:
            state = 0;
            time = 4000;
            break;
        case 1:
        case 3:
            if (wifiState == wifiGotIP)
                state = 1;
            else
                state = 0;
            time = 25;
            break;
        }
        break;
    case SOFTAP_MODE:
        state = 1;
        time = 2000;
        break;
    case STATIONAP_MODE:
        if (wifiState == wifiGotIP) {
            switch (ledPhase) {
            case 0:
                state = 0;
                time = 2000;
                break;
            case 1:
                state = 1;
                time = 25;
                break;
            case 2:
                state = 0;
                time = 150;
                break;
            case 3:
                state = 1;
                time = 2000;
                break;
            }
        }
        else {
            switch (ledPhase) {
            case 0:
            case 2:
                state = 0;
                break;
            case 1:
            case 3:
                state = 1;
                break;
            }
            time = 2000;
        }
        break;
    }

    setLed(state);
    ledPhase = (ledPhase + 1) % 4;
    os_timer_arm(&ledTimer, time, 0);
}

// change the wifi state indication
void ICACHE_FLASH_ATTR statusWifiUpdate(uint8_t state) {
  wifiState = state;
  // schedule an update (don't want to run into concurrency issues)
  os_timer_disarm(&ledTimer);
  os_timer_setfn(&ledTimer, ledTimerCb, NULL);
  os_timer_arm(&ledTimer, 500, 0);
}

// handler for wifi status change callback coming in from espressif library
static void ICACHE_FLASH_ATTR wifiHandleEventCb(System_Event_t *evt) {
  switch (evt->event) {
  case EVENT_STAMODE_CONNECTED:
    statusWifiUpdate(wifiIsConnected);
    break;
  case EVENT_STAMODE_DISCONNECTED:
    statusWifiUpdate(wifiIsDisconnected);
    break;
  case EVENT_STAMODE_AUTHMODE_CHANGE:
    break;
  case EVENT_STAMODE_GOT_IP:
    statusWifiUpdate(wifiGotIP);
    break;
  case EVENT_SOFTAPMODE_STACONNECTED:
    break;
  case EVENT_SOFTAPMODE_STADISCONNECTED:
    break;
  default:
    break;
  }
}

//===== Init status stuff

void ICACHE_FLASH_ATTR statusInit(void) {
  if (flashConfig.conn_led_pin >= 0) {
    makeGpio(flashConfig.conn_led_pin);
    setLed(1);
  }
#ifdef STATUS_DBG
  os_printf("CONN led=%d\n", flashConfig.conn_led_pin);
#endif

  os_timer_disarm(&ledTimer);
  os_timer_setfn(&ledTimer, ledTimerCb, NULL);
  os_timer_arm(&ledTimer, 2000, 0);

  wifi_set_event_handler_cb(wifiHandleEventCb);

#ifdef MQTT
  os_timer_disarm(&mqttStatusTimer);
  os_timer_setfn(&mqttStatusTimer, mqttStatusCb, NULL);
  os_timer_arm(&mqttStatusTimer, MQTT_STATUS_INTERVAL, 1); // recurring timer
#endif // MQTT
}


