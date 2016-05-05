typedef struct {
    int baud_rate;
    int reset_pin;
    char *hostname;
    char *sys_descr;
    int rx_pullup;
} Config;

extern Config flashConfig;
