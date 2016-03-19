#ifndef CGIWIFI_H
#define CGIWIFI_H

#include "httpd.h"

enum { wifiIsDisconnected, wifiIsConnected, wifiGotIP };
typedef void(*WifiStateChangeCb)(uint8_t wifiStatus);

void configWifiIP();
void wifiInit(void);
void wifiAddStateChangeCb(WifiStateChangeCb cb);
int checkString(char *str);

extern uint8_t wifiState;

#endif
