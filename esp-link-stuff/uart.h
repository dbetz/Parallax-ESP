#ifndef __UART_H__
#define __UART_H__

#include "uart_hw.h"

// Receive callback function signature
typedef void (*UartRecv_cb)(char *buf, short len);

// current baud rate
extern int uart0_baudRate;

// Initialize UARTs to the provided baud rates (115200 recommended). This also makes the os_printf
// calls use uart1 for output (for debugging purposes)
void uart_init(UartBaudRate uart0_br, UartBaudRate uart1_br);

void uart_tx_buffer(uint8 uart, char *buf, uint16 len);
STATUS uart_tx_one_char(uint8 uart, uint8 c);
STATUS uart_try_tx_one_char(uint8 uart, uint8 c);
STATUS uart_drain_tx_buffer(uint8 uart);

// Add a receive callback function, this is called on the uart receive task each time a chunk
// of bytes are received. A small number of callbacks can be added and they are all called
// with all new characters.
void uart_add_recv_cb(UartRecv_cb cb);

// Turn UART interrupts off and poll for nchars or until timeout hits
uint16_t uart0_rx_poll(char *buff, uint16_t nchars, uint32_t timeout_us);

void uart0_baud(int rate);

#endif /* __UART_H__ */
