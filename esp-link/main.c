/*
* ----------------------------------------------------------------------------
* "THE BEER-WARE LICENSE" (Revision 42):
* Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain
* this notice you can do whatever you want with this stuff. If we meet some day,
* and you think this stuff is worth it, you can buy me a beer in return.
* ----------------------------------------------------------------------------
* Heavily modified and enhanced by Thorsten von Eicken in 2015
* ----------------------------------------------------------------------------
*/

#include <esp8266.h>
#include "httpd.h"
#include "cgi.h"
#include "cgiwifi.h"
#include "cgiflash.h"
#include "safeupgrade.h"
#include "auth.h"
#include "uart.h"
#include "gpio.h"
#include "stringdefs.h"

#define NOTICE(format, ...) do {	                                          \
	os_printf(format "\n", ## __VA_ARGS__);                                   \
} while ( 0 )

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
HttpdBuiltInUrl builtInUrls[] = {
  { "/flash/next", cgiGetFirmwareNext, NULL },
  { "/flash/upload", cgiUploadFirmware, NULL },
  { "/flash/reboot", cgiRebootFirmware, NULL },
  { NULL, NULL, NULL }
};

# define VERS_STR_STR(V) #V
# define VERS_STR(V) VERS_STR_STR(V)
static const char* const esp_link_version = VERS_STR(VERSION);


void ICACHE_FLASH_ATTR user_rf_pre_init(void) {
  /* undo upgrade, if the first boot failes
   * with an watchdog reset, soft watchdog reset or an exception
   */
  cgiFlashCheckUpgradeHealthy();

  //default is enabled
  system_set_os_print(DEBUG_SDK);
}

// Main routine to initialize esp-link.
void ICACHE_FLASH_ATTR user_init(void) {
  // Init gpio pin registers
  gpio_init();
  gpio_output_set(0, 0, 0, (1<<15)); // some people tie it to GND, gotta ensure it's disabled
  // init UART
  uart_init(115200, 115200);
  // Say hello (leave some time to cause break in TX after boot loader's msg
  os_delay_us(10000L);
  NOTICE("\n\n** %s\n", esp_link_version);
  // Wifi
  wifiInit();

  // mount the http handlers
  httpdInit(builtInUrls, 80);

  struct rst_info *rst_info = system_get_rst_info();
  NOTICE("Reset cause: %d=%s", rst_info->reason, rst_codes[rst_info->reason]);
  NOTICE("exccause=%d epc1=0x%x epc2=0x%x epc3=0x%x excvaddr=0x%x depc=0x%x",
    rst_info->exccause, rst_info->epc1, rst_info->epc2, rst_info->epc3,
    rst_info->excvaddr, rst_info->depc);
  uint32_t fid = spi_flash_get_id();
  NOTICE("Flash map %s, manuf 0x%02lX chip 0x%04lX", flash_maps[system_get_flash_size_map()],
      fid & 0xff, (fid&0xff00)|((fid>>16)&0xff));
}
