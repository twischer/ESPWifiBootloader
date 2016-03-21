/*
 * File : uart.c
 * This file is part of Espressif's AT+ command set program.
 * Copyright (C) 2013 - 2016, Espressif Systems
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of version 3 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 * Heavily modified and enhanced by Thorsten von Eicken in 2015
 */
#include "esp8266.h"
#include "uart.h"

#ifdef UART_DBG
#define DBG_UART(format, ...) os_printf(format, ## __VA_ARGS__)
#else
#define DBG_UART(format, ...) do { } while(0)
#endif

// UartDev is defined and initialized in rom code.
extern UartDevice    UartDev;


/******************************************************************************
 * FunctionName : uart_config
 * Description  : Internal used function
 *                UART0 used for data TX/RX, RX buffer size is 0x100, interrupt enabled
 *                UART1 just used for debug output
 * Parameters   : uart_no, use UART0 or UART1 defined ahead
 * Returns      : NONE
*******************************************************************************/
static void ICACHE_FLASH_ATTR
uart_config0()
{
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, 0); // FUNC_U0RXD==0

  uart_div_modify(UART0, UART_CLK_FREQ / UartDev.baut_rate);

    WRITE_PERI_REG(UART_CONF0(UART0),
        CALC_UARTMODE(UartDev.data_bits, UartDev.parity, UartDev.stop_bits));

  //clear rx and tx fifo,not ready
  SET_PERI_REG_MASK(UART_CONF0(UART0), UART_RXFIFO_RST | UART_TXFIFO_RST);
  CLEAR_PERI_REG_MASK(UART_CONF0(UART0), UART_RXFIFO_RST | UART_TXFIFO_RST);

    // Configure RX interrupt conditions as follows: trigger rx-full when there are 80 characters
    // in the buffer, trigger rx-timeout when the fifo is non-empty and nothing further has been
    // received for 4 character periods.
    // Set the hardware flow-control to trigger when the FIFO holds 100 characters, although
    // we don't really expect the signals to actually be wired up to anything. It doesn't hurt
    // to set the threshold here...
    // We do not enable framing error interrupts 'cause they tend to cause an interrupt avalanche
    // and instead just poll for them when we get a std RX interrupt.
    WRITE_PERI_REG(UART_CONF1(UART0),
                   ((80 & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S) |
                   ((100 & UART_RX_FLOW_THRHD) << UART_RX_FLOW_THRHD_S) |
                   UART_RX_FLOW_EN |
                   (4 & UART_RX_TOUT_THRHD) << UART_RX_TOUT_THRHD_S |
                   UART_RX_TOUT_EN);


  //clear all interrupt
  WRITE_PERI_REG(UART_INT_CLR(UART0), 0xffff);
}

/******************************************************************************
 * FunctionName : uart_init
 * Description  : user interface for init uart
 * Parameters   : UartBautRate uart0_br - uart0 bautrate
 *                UartBautRate uart1_br - uart1 bautrate
 * Returns      : NONE
*******************************************************************************/
void ICACHE_FLASH_ATTR
uart_init(UartBautRate uart0_br)
{
  // rom use 74880 baut_rate, here reinitialize
  UartDev.baut_rate = uart0_br;
  uart_config0();
}
