#ifndef CONFIG_H
#define CONFIG_H

// Flash configuration settings. When adding new items always add them at the end and formulate
// them such that a value of zero is an appropriate default or backwards compatible. Existing
// modules that are upgraded will have zero in the new fields. This ensures that an upgrade does
// not wipe out the old settings.
typedef struct {
  uint32_t seq; // flash write sequence number
  uint16_t magic, crc;
  uint32_t version;
  int32_t  loader_baud_rate;
  int32_t  baud_rate;
  int32_t  dbg_baud_rate;
  int8_t   stop_bits;
  int8_t   dbg_stop_bits;
  int8_t   conn_led_pin;
  int8_t   reset_pin;
  char     module_name[32+1];
  char     module_descr[129];
  int8_t   rx_pullup;
  int8_t   sscp_enable;
  char     sscp_need_pause[16];
  int8_t   sscp_need_pause_cnt;
  int32_t  sscp_pause_time_ms;
  uint8_t  sscp_start;
  int8_t   sscp_events;
  int8_t   dbg_enable;
  int8_t   sscp_loader;
  int8_t   p2_ddloader_enable;
} FlashConfig;

extern FlashConfig flashConfig;
extern FlashConfig flashDefault;

bool configSave(void);
bool configRestore(void);
bool configRestoreDefaults(void);
void configWipe(void);
const size_t getFlashSize();

bool softap_get_ssid(char *ssid, int size);
bool softap_set_ssid(const char *ssid, int size);

#endif
