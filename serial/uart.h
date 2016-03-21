#ifndef __UART_H__
#define __UART_H__

#include "uart_hw.h"

// Initialize UARTs to the provided baud rates (115200 recommended). This also makes the os_printf
// calls use uart1 for output (for debugging purposes)
void uart_init(UartBautRate uart0_br, UartBautRate uart1_br);

#endif /* __UART_H__ */
