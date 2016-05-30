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
  int8_t   reset_pin;
  int32_t  baud_rate;
  int32_t  loader_baud_rate;
  int8_t   conn_led_pin;
  char     module_name[32];
  char     module_descr[129];
  int8_t   rx_pullup;
  int8_t   enable_sscp;
  char     sscp_need_pause[16];
  int8_t   sscp_need_pause_cnt;
  int32_t  sscp_pause_time_ms;
} FlashConfig;

extern FlashConfig flashConfig;
extern FlashConfig flashDefault;

bool configSave(void);
bool configRestore(void);
void configWipe(void);
const size_t getFlashSize();

#endif
