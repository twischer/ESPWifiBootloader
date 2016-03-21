#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_
#include <c_types.h>
#ifdef __WIN32__
#include <_mingw.h>
#endif


#define SDK_DBG

#undef CGI_DBG
#undef CGIFLASH_DBG
#undef CGIWIFI_DBG
#undef HTTPD_DBG
#undef UART_DBG
#undef SAFE_UPGRADE_DBG


#endif
