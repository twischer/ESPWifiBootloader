/*
Some random cgi routines.
*/

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
#include "cgi.h"

#ifdef CGI_DBG
#define DBG(format, ...) do { os_printf(format, ## __VA_ARGS__); } while(0)
#else
#define DBG(format, ...) do { } while(0)
#endif

void ICACHE_FLASH_ATTR noCacheHeaders(HttpdConnData *connData, int code) {
  httpdStartResponse(connData, code);
  httpdHeader(connData, "Cache-Control", "no-cache, no-store, must-revalidate");
  httpdHeader(connData, "Pragma", "no-cache");
  httpdHeader(connData, "Expires", "0");
}

void ICACHE_FLASH_ATTR jsonHeader(HttpdConnData *connData, int code) {
  noCacheHeaders(connData, code);
  httpdHeader(connData, "Content-Type", "application/json");
  httpdEndHeaders(connData);
}

void ICACHE_FLASH_ATTR errorResponse(HttpdConnData *connData, int code, char *message) {
  noCacheHeaders(connData, code);
  httpdEndHeaders(connData);
  httpdSend(connData, message, -1);
  DBG("HTTP %d error response: \"%s\"\n", code, message);
}

// look for the HTTP arg 'name' and store it at 'config' with max length 'max_len' (incl
// terminating zero), returns -1 on error, 0 if not found, 1 if found and OK
int8_t ICACHE_FLASH_ATTR getStringArg(HttpdConnData *connData, char *name, char *config, int max_len) {
  char buff[128];
  int len = httpdFindArg(connData->getArgs, name, buff, sizeof(buff));
  if (len < 0) return 0; // not found, skip
  if (len >= max_len) {
    os_sprintf(buff, "Value for %s too long (%d > %d allowed)", name, len, max_len-1);
    errorResponse(connData, 400, buff);
    return -1;
  }
  os_strcpy(config, buff);
  return 1;
}

// look for the HTTP arg 'name' and store it at 'config' as an 8-bit integer
// returns -1 on error, 0 if not found, 1 if found and OK
int8_t ICACHE_FLASH_ATTR getInt8Arg(HttpdConnData *connData, char *name, int8_t *config) {
  char buff[16];
  int len = httpdFindArg(connData->getArgs, name, buff, sizeof(buff));
  if (len < 0) return 0; // not found, skip
  int m = atoi(buff);
  if (len > 5 || m < -127 || m > 127) {
    os_sprintf(buff, "Value for %s out of range", name);
    errorResponse(connData, 400, buff);
    return -1;
  }
  *config = m;
  return 1;
}

// look for the HTTP arg 'name' and store it at 'config' as an unsigned 8-bit integer
// returns -1 on error, 0 if not found, 1 if found and OK
int8_t ICACHE_FLASH_ATTR getUInt8Arg(HttpdConnData *connData, char *name, uint8_t *config) {
  char buff[16];
  int len = httpdFindArg(connData->getArgs, name, buff, sizeof(buff));
  if (len < 0) return 0; // not found, skip
  int m = atoi(buff);
  if (len > 4 || m < 0 || m > 255) {
    os_sprintf(buff, "Value for %s out of range", name);
    errorResponse(connData, 400, buff);
    return -1;
  }
  *config = m;
  return 1;
}

// look for the HTTP arg 'name' and store it at 'config' as an unsigned 16-bit integer
// returns -1 on error, 0 if not found, 1 if found and OK
int8_t ICACHE_FLASH_ATTR getUInt16Arg(HttpdConnData *connData, char *name, uint16_t *config) {
  char buff[16];
  int len = httpdFindArg(connData->getArgs, name, buff, sizeof(buff));
  if (len < 0) return 0; // not found, skip
  int m = atoi(buff);
  if (len > 6 || m < 0 || m > 65535) {
    os_sprintf(buff, "Value for %s out of range", name);
    errorResponse(connData, 400, buff);
    return -1;
  }
  *config = m;
  return 1;
}

int8_t ICACHE_FLASH_ATTR getBoolArg(HttpdConnData *connData, char *name, bool *config) {
  char buff[16];
  int len = httpdFindArg(connData->getArgs, name, buff, sizeof(buff));
  if (len < 0) return 0; // not found, skip

  if (os_strcmp(buff, "1") == 0 || os_strcmp(buff, "true") == 0) {
    *config = true;
    return 1;
  }

  if (os_strcmp(buff, "0") == 0 || os_strcmp(buff, "false") == 0) {
    *config = false;
    return 1;
  }

  os_sprintf(buff, "Invalid value for %s", name);
  errorResponse(connData, 400, buff);
  return -1;
}

uint8_t ICACHE_FLASH_ATTR UTILS_StrToIP(const char* str, void *ip){
  /* The count of the number of bytes processed. */
  int i;
  /* A pointer to the next digit to process. */
  const char * start;

  start = str;
  for (i = 0; i < 4; i++) {
    /* The digit being processed. */
    char c;
    /* The value of this byte. */
    int n = 0;
    while (1) {
      c = *start;
      start++;
      if (c >= '0' && c <= '9') {
        n *= 10;
        n += c - '0';
      }
      /* We insist on stopping at "." if we are still parsing
      the first, second, or third numbers. If we have reached
      the end of the numbers, we will allow any character. */
      else if ((i < 3 && c == '.') || i == 3) {
        break;
      }
      else {
        return 0;
      }
    }
    if (n >= 256) {
      return 0;
    }
    ((uint8_t*)ip)[i] = n;
  }
  return 1;
}
