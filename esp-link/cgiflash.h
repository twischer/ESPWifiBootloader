#ifndef CGIFLASH_H
#define CGIFLASH_H

#include "httpd.h"

const char* const checkUpgradedFirmware(void);

int cgiGetFirmwareNext(HttpdConnData *connData);
int cgiUploadFirmware(HttpdConnData *connData);
int cgiRebootFirmware(HttpdConnData *connData);

#endif
