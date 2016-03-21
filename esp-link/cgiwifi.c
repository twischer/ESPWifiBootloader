/*
Cgi/template routines for the /wifi url.
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
#include "cgiwifi.h"
#include "cgi.h"

#ifdef CGIWIFI_DBG
#define DBG(format, ...) do { os_printf(format, ## __VA_ARGS__); } while(0)
#else
#define DBG(format, ...) do { } while(0)
#endif

# define VERS_STR_STR(V) #V
# define VERS_STR(V) VERS_STR_STR(V)

bool mdns_started = false;

// ===== wifi status change callbacks
static WifiStateChangeCb wifi_state_change_cb[4];

// Temp store for new staion config
struct station_config stconf;

// Temp store for new ap config
struct softap_config apconf;

uint8_t wifiState = wifiIsDisconnected;
// reasons for which a connection failed
uint8_t wifiReason = 0;
static char *wifiReasons[] = {
  "", "unspecified", "auth_expire", "auth_leave", "assoc_expire", "assoc_toomany", "not_authed",
  "not_assoced", "assoc_leave", "assoc_not_authed", "disassoc_pwrcap_bad", "disassoc_supchan_bad",
  "ie_invalid", "mic_failure", "4way_handshake_timeout", "group_key_update_timeout",
  "ie_in_4way_differs", "group_cipher_invalid", "pairwise_cipher_invalid", "akmp_invalid",
  "unsupp_rsn_ie_version", "invalid_rsn_ie_cap", "802_1x_auth_failed", "cipher_suite_rejected",
  "beacon_timeout", "no_ap_found" };

static char *wifiMode[] = { 0, "STA", "AP", "AP+STA" };

void (*wifiStatusCb)(uint8_t); // callback when wifi status changes

static char* ICACHE_FLASH_ATTR wifiGetReason(void) {
  if (wifiReason <= 24) return wifiReasons[wifiReason];
  if (wifiReason >= 200 && wifiReason <= 201) return wifiReasons[wifiReason-200+24];
  return wifiReasons[1];
}

// handler for wifi status change callback coming in from espressif library
static void ICACHE_FLASH_ATTR wifiHandleEventCb(System_Event_t *evt) {
  switch (evt->event) {
  case EVENT_STAMODE_CONNECTED:
    wifiState = wifiIsConnected;
    wifiReason = 0;
    DBG("Wifi connected to ssid %s, ch %d\n", evt->event_info.connected.ssid,
      evt->event_info.connected.channel);
    break;
  case EVENT_STAMODE_DISCONNECTED:
    wifiState = wifiIsDisconnected;
    wifiReason = evt->event_info.disconnected.reason;
    DBG("Wifi disconnected from ssid %s, reason %s (%d)\n",
      evt->event_info.disconnected.ssid, wifiGetReason(), evt->event_info.disconnected.reason);
    break;
  case EVENT_STAMODE_AUTHMODE_CHANGE:
    DBG("Wifi auth mode: %d -> %d\n",
      evt->event_info.auth_change.old_mode, evt->event_info.auth_change.new_mode);
    break;
  case EVENT_STAMODE_GOT_IP:
    wifiState = wifiGotIP;
    wifiReason = 0;
    DBG("Wifi got ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR "\n",
      IP2STR(&evt->event_info.got_ip.ip), IP2STR(&evt->event_info.got_ip.mask),
      IP2STR(&evt->event_info.got_ip.gw));
//    if (!mdns_started)
//      wifiStartMDNS(evt->event_info.got_ip.ip);
    break;
  case EVENT_SOFTAPMODE_STACONNECTED:
    DBG("Wifi AP: station " MACSTR " joined, AID = %d\n",
        MAC2STR(evt->event_info.sta_connected.mac), evt->event_info.sta_connected.aid);
    break;
  case EVENT_SOFTAPMODE_STADISCONNECTED:
    DBG("Wifi AP: station " MACSTR " left, AID = %d\n",
        MAC2STR(evt->event_info.sta_disconnected.mac), evt->event_info.sta_disconnected.aid);
    break;
  default:
    break;
  }

  for (int i = 0; i < 4; i++) {
    if (wifi_state_change_cb[i] != NULL) (wifi_state_change_cb[i])(wifiState);
  }
}

void ICACHE_FLASH_ATTR wifiAddStateChangeCb(WifiStateChangeCb cb) {
  for (int i = 0; i < 4; i++) {
    if (wifi_state_change_cb[i] == cb) return;
    if (wifi_state_change_cb[i] == NULL) {
      wifi_state_change_cb[i] = cb;
      return;
    }
  }
  DBG("WIFI: max state change cb count exceeded\n");
}

//void ICACHE_FLASH_ATTR wifiStartMDNS(struct ip_addr ip) {
//  if (flashConfig.mdns_enable) {
//    struct mdns_info *mdns_info = (struct mdns_info *)os_zalloc(sizeof(struct mdns_info));
//    mdns_info->host_name = flashConfig.hostname;
//    mdns_info->server_name = flashConfig.mdns_servername;
//    mdns_info->server_port = 80;
//    mdns_info->ipAddr = ip.addr;
//    espconn_mdns_init(mdns_info);
//  }
//  else {
//    espconn_mdns_server_unregister();
//    espconn_mdns_close();
//  }
//  mdns_started = true;
//}

// ===== timers to change state and rescue from failed associations

// reset timer changes back to STA+AP if we can't associate
#define RESET_TIMEOUT (15000) // 15 seconds
static ETSTimer resetTimer;

// This routine is ran some time after a connection attempt to an access point. If
// the connect succeeds, this gets the module in STA-only mode. If it fails, it ensures
// that the module is in STA+AP mode so the user has a chance to recover.
static void ICACHE_FLASH_ATTR resetTimerCb(void *arg) {
  int x = wifi_station_get_connect_status();
  int m = wifi_get_opmode() & 0x3;
  DBG("Wifi check: mode=%s status=%d\n", wifiMode[m], x);

  if(m!=2){
    if ( x == STATION_GOT_IP ) {
      if (m != 1) {
#ifdef CHANGE_TO_STA
      // We're happily connected, go to STA mode
      DBG("Wifi got IP. Going into STA mode..\n");
      wifi_set_opmode(1);
      os_timer_arm(&resetTimer, RESET_TIMEOUT, 0); // check one more time after switching to STA-only
#endif
    }
    // no more resetTimer at this point, gotta use physical reset to recover if in trouble
 } else {
   if (m != 3) {
       DBG("Wifi connect failed. Going into STA+AP mode..\n");
       wifi_set_opmode(3);
       wifi_softap_set_config(&apconf);
    }
    DBG("Enabling/continuing uart log\n");
    os_timer_arm(&resetTimer, RESET_TIMEOUT, 0);
    }
  }
}

// Reassociate timer to delay change of association so the original request can finish
static ETSTimer reassTimer;

// Callback actually doing reassociation
static void ICACHE_FLASH_ATTR reassTimerCb(void *arg) {
  DBG("Wifi changing association\n");
  wifi_station_disconnect();
  stconf.bssid_set = 0;
  wifi_station_set_config(&stconf);
  wifi_station_connect();
  // Schedule check, we give some extra time (4x) 'cause the reassociation can cause the AP
  // to have to change channel, and then the client needs to follow before it can see the
  // IP address
  os_timer_disarm(&resetTimer);
  os_timer_setfn(&resetTimer, resetTimerCb, NULL);
  os_timer_arm(&resetTimer, 4*RESET_TIMEOUT, 0);
}

// This cgi uses the routines above to connect to a specific access point with the
// given ESSID using the given password.
int ICACHE_FLASH_ATTR cgiWiFiConnect(HttpdConnData *connData) {
    int mode = wifi_get_opmode();
    if(mode == 2){
        jsonHeader(connData, 400);
        httpdSend(connData, "Can't associate to an AP en SoftAP mode", -1);
        return HTTPD_CGI_DONE;
    }
  char essid[128];
  char passwd[128];

  if (connData->conn==NULL) return HTTPD_CGI_DONE;

  int el = httpdFindArg(connData->getArgs, "essid", essid, sizeof(essid));
  int pl = httpdFindArg(connData->getArgs, "passwd", passwd, sizeof(passwd));

  if (el > 0 && pl >= 0) {
    //Set to 0 if you want to disable the actual reconnecting bit
    os_strncpy((char*)stconf.ssid, essid, 32);
    os_strncpy((char*)stconf.password, passwd, 64);
    DBG("Wifi try to connect to AP %s pw %s\n", essid, passwd);

    //Schedule disconnect/connect
    os_timer_disarm(&reassTimer);
    os_timer_setfn(&reassTimer, reassTimerCb, NULL);
    os_timer_arm(&reassTimer, 1000, 0); // 1 second for the response of this request to make it
    jsonHeader(connData, 200);
  } else {
    jsonHeader(connData, 400);
    httpdSend(connData, "Cannot parse ssid or password", -1);
  }
  return HTTPD_CGI_DONE;
}

#ifdef DEBUGIP
static void ICACHE_FLASH_ATTR debugIP() {
  struct ip_info info;
  if (wifi_get_ip_info(0, &info)) {
    os_printf("\"ip\": \"%d.%d.%d.%d\"\n", IP2STR(&info.ip.addr));
    os_printf("\"netmask\": \"%d.%d.%d.%d\"\n", IP2STR(&info.netmask.addr));
    os_printf("\"gateway\": \"%d.%d.%d.%d\"\n", IP2STR(&info.gw.addr));
    os_printf("\"hostname\": \"%s\"\n", wifi_station_get_hostname());
  } else {
    os_printf("\"ip\": \"-none-\"\n");
  }
}
#endif

// configure Wifi, specifically DHCP vs static IP address based on flash config
void ICACHE_FLASH_ATTR configWifiIP() {
//  if (flashConfig.staticip == 0) {
    // let's DHCP!
    wifi_station_set_hostname(ESP_HOSTNAME);
    if (wifi_station_dhcpc_status() == DHCP_STARTED)
      wifi_station_dhcpc_stop();
    wifi_station_dhcpc_start();
    DBG("Wifi uses DHCP, hostname=%s\n", ESP_HOSTNAME);
//  } else {
//    // no DHCP, we got static network config!
//    wifi_station_dhcpc_stop();
//    struct ip_info ipi;
//    ipi.ip.addr = flashConfig.staticip;
//    ipi.netmask.addr = flashConfig.netmask;
//    ipi.gw.addr = flashConfig.gateway;
//    wifi_set_ip_info(0, &ipi);
//    DBG("Wifi uses static IP %d.%d.%d.%d\n", IP2STR(&ipi.ip.addr));
//  }
#ifdef DEBUGIP
  debugIP();
#endif
}

#ifdef CHANGE_TO_STA
#define MODECHANGE "yes"
#else
#define MODECHANGE "no"
#endif

// Check string againt invalid characters
int ICACHE_FLASH_ATTR checkString(char *str){
    int i = 0;
    for(; i < os_strlen(str); i++)
    {
        // Alphanumeric and underscore allowed
        if (!(isalnum((unsigned char)str[i]) || str[i] == '_'))
        {
            DBG("Error: String has non alphanumeric chars\n");
            return 0;
        }
    }
    return 1;
}

/*  Init the wireless
 *
 *  Call both Soft-AP and Station default config
 *  Change values according to Makefile hard-coded variables
 *  Anyway set wifi opmode to STA+AP, it will change to STA if CHANGE_TO_STA is set to yes in Makefile
 *  Call a timer to check the STA connection
 */
void ICACHE_FLASH_ATTR wifiInit() {

    // Check te wifi opmode
    int x = wifi_get_opmode() & 0x3;

    // Set opmode to 3 to let system scan aps, otherwise it won't scan
    wifi_set_opmode(3);

    // Call both STATION and SOFTAP default config
    wifi_station_get_config_default(&stconf);
    wifi_softap_get_config_default(&apconf);

    DBG("Wifi init, mode=%s\n",wifiMode[x]);

    // STATION parameters
#if defined(STA_SSID) && defined(STA_PASS)
    // Set parameters
    if (os_strlen((char*)stconf.ssid) == 0 && os_strlen((char*)stconf.password) == 0) {
        os_strncpy((char*)stconf.ssid, VERS_STR(STA_SSID), 32);
        os_strncpy((char*)stconf.password, VERS_STR(STA_PASS), 64);

        DBG("Wifi pre-config trying to connect to AP %s pw %s\n",(char*)stconf.ssid, (char*)stconf.password);

        // wifi_set_phy_mode(2); // limit to 802.11b/g 'cause n is flaky
        stconf.bssid_set = 0;
        wifi_station_set_config(&stconf);
    }
#endif

    // Change SOFT_AP settings if defined
#if defined(AP_SSID)
    // Check if ssid and pass are alphanumeric values
    int ssidlen = os_strlen(VERS_STR(AP_SSID));
    if(checkString(VERS_STR(AP_SSID)) && ssidlen > 7 && ssidlen < 32){
        // Clean memory and set the value of SSID
        os_memset(apconf.ssid, 0, 32);
        os_memcpy(apconf.ssid, VERS_STR(AP_SSID), os_strlen(VERS_STR(AP_SSID)));
        // Specify the length of ssid
        apconf.ssid_len= ssidlen;
#if defined(AP_PASS)
        // If pass is at least 8 and less than 64
        int passlen = os_strlen(VERS_STR(AP_PASS));
        if( checkString(VERS_STR(AP_PASS)) && passlen > 7 && passlen < 64 ){
            // Clean memory and set the value of PASS
            os_memset(apconf.password, 0, 64);
            os_memcpy(apconf.password, VERS_STR(AP_PASS), passlen);
            // Can't choose auth mode without a valid ssid and password
#ifdef AP_AUTH_MODE
            // If set, use specified auth mode
            if(AP_AUTH_MODE >= 0 && AP_AUTH_MODE <=4)
                apconf.authmode = AP_AUTH_MODE;
#else
            // If not, use WPA2
            apconf.authmode = AUTH_WPA_WPA2_PSK;
#endif
        }else if ( passlen == 0){
            // If ssid is ok and no pass, set auth open
            apconf.authmode = AUTH_OPEN;
            // Remove stored password
            os_memset(apconf.password, 0, 64);
        }
#endif
    }// end of ssid and pass check
#ifdef AP_SSID_HIDDEN
    // If set, use specified ssid hidden parameter
    if(AP_SSID_HIDDEN == 0 || AP_SSID_HIDDEN ==1)
        apconf.ssid_hidden = AP_SSID_HIDDEN;
#endif
#ifdef AP_MAX_CONN
    // If set, use specified max conn number
    if(AP_MAX_CONN > 0 && AP_MAX_CONN <5)
        apconf.max_connection = AP_MAX_CONN;
#endif
#ifdef AP_BEACON_INTERVAL
    // If set use specified beacon interval
    if(AP_BEACON_INTERVAL >= 100 && AP_BEACON_INTERVAL <= 60000)
        apconf.beacon_interval = AP_BEACON_INTERVAL;
#endif
    // Check softap config
    bool softap_set_conf = wifi_softap_set_config(&apconf);
    // Debug info

    DBG("Wifi Soft-AP parameters change: %s\n",softap_set_conf? "success":"fail");
#endif // AP_SSID && AP_PASS

    configWifiIP();

    // The default sleep mode should be modem_sleep, but we set it here explicitly for good
    // measure. We can't use light_sleep because that powers off everthing and we would loose
    // all connections.
    wifi_set_sleep_type(MODEM_SLEEP_T);

    wifi_set_event_handler_cb(wifiHandleEventCb);
    // check on the wifi in a few seconds to see whether we need to switch mode
    os_timer_disarm(&resetTimer);
    os_timer_setfn(&resetTimer, resetTimerCb, NULL);
    os_timer_arm(&resetTimer, RESET_TIMEOUT, 0);
}
