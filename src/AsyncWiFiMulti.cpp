#include "AsyncWiFiMulti.h"
#include <functional>
#include <Arduino.h>

#define _MAC_ "%02x:%02x:%02x:%02x:%02x:%02x"

using namespace std::placeholders;

AsyncWiFiMulti::AsyncWiFiMulti(unsigned long reconnect) {

  reconnectDelay=reconnect;
  if (reconnectDelay) {
    onConnected([this](const AsyncWiFiMulti::ApSettings &ap) {
      this->onConnectedDefault(ap);
    });

    onFailure([this]() {
      this->onFailureDefault();
    });

    onDisconnected([this](const char *ssid, uint8_t disconnectionReason) {
      this->onDisconnectedDefault(ssid, disconnectionReason);
    });
  }
}

AsyncWiFiMulti::~AsyncWiFiMulti() {
  WiFi.removeEvent(event_id);
}

void AsyncWiFiMulti::onConnectedDefault(const AsyncWiFiMulti::ApSettings& ap) {
  printf("Connected to [" _MAC_ "] %s\n",
    ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
    ap.ssid.c_str());
}

void AsyncWiFiMulti::onFailureDefault() {
  delay(reconnectDelay); 
  start();
}

void AsyncWiFiMulti::onDisconnectedDefault(const char *ssid, uint8_t disconnectionReason) {
  delay(reconnectDelay); 
  start();
}

bool AsyncWiFiMulti::addAP(const char *ssid, const char *passphrase) {
  ApSettings newAP;
  newAP.ssid       = ssid;
  newAP.passphrase = passphrase;

  if(!newAP.valid()) {
    log_i("[AWM][APlistAdd] invalid AP %s, skipping", newAP.ssid.c_str());
    return false;
  }

  if(std::any_of(configuredAPs.begin(), configuredAPs.end(), [&newAP](const ApSettings &ap) {
    return ap.ssid == newAP.ssid;
  })) {
    log_i("[AWM][APlistAdd] AP %s already exists, skipping", newAP.ssid.c_str());
    return false;
  }
  log_i("[AWM][APlistAdd] add SSID: %s", newAP.ssid.c_str()); 
  configuredAPs.push_front(newAP);
  return true;
}

bool AsyncWiFiMulti::run() {
  return start();
}

bool AsyncWiFiMulti::start() {
  if(!event_id) {
    event_id = WiFi.onEvent(std::bind(&AsyncWiFiMulti::onEvent, this, _1, _2));
    log_v("[AWM][start] Registered WiFi event handler with ID %d", event_id);
  }
  if(_status == Status::Running) {
    log_i("[AWM][start] AsyncWiFiMulti already running");
    return false;
  }
  if(_status == Status::Connected) {
    log_i("[AWM][start] AsyncWiFiMulti already connected, disconnect first");
    return false;
  }
  log_i("[AWM][start] Starting AsyncWiFiMulti with %d configured APs", configuredAPs.size());
  WiFi.disconnect(false, false);
  delay(10); // Ensure WiFi is disconnected before starting
  WiFi.mode(WIFI_STA);
  return rescan();
}

bool AsyncWiFiMulti::rescan() {
  if(!event_id) {
    log_e("[AWM][rescan] No event handler registered, call start() first");
    return false;
  }
  if(_status == Status::Running) {
    log_e("A[AWM][rescan] syncWiFiMulti already running");
    return false;
  }
  if(_status == Status::Connected) {
    log_e("[AWM][rescan] AsyncWiFiMulti already connected, disconnect first");
    return false;
  }
  WiFi.scanNetworks(true, true);
  
  _status = Running;
  return true;
}

void AsyncWiFiMulti::clear() {
  log_v("[AWM][clear] Clearing configured APs");
  configuredAPs.clear();
  foundAPs.clear();
  currentAp = foundAPs.end();
  _status = Idle;
}

bool AsyncWiFiMulti::ApSettings::valid() const {
  if(ssid.empty() || ssid.length() == 0 || ssid.length() > 31) {
    return false;
  }
  if(passphrase.length() > 63) {
    return false;
  }
  return true;
}

void AsyncWiFiMulti::onEvent(arduino_event_id_t event_type, const arduino_event_info_t &event_info){
  char ssid[33] = {0};

  if(_status == Connected) {
    if(event_type == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      memcpy(ssid, event_info.wifi_sta_disconnected.ssid, event_info.wifi_sta_disconnected.ssid_len);
      log_i("[AWM][onEvent] WiFi disconnected: ssid=`%s`, reason: %s, status: %s ", 
        ssid,
        WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(event_info.wifi_sta_disconnected.reason)), 
        statusString());
      _status = Idle;
      if(onDisconnectedCallback) {
        onDisconnectedCallback(ssid, event_info.wifi_sta_disconnected.reason);
      }
    }
    if(event_type == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
      log_i("[AWM][onEvent] WiFi: lost IP event received, status: %s", statusString());
      _status = Idle;
      if(onDisconnectedCallback) {
        onDisconnectedCallback(WiFi.SSID().c_str(), -1);
      }
    }
    return;
  }
  log_d("[AWM][onEvent] Event received: %s, status: %s", WiFi.eventName(event_type), statusString());
  if(_status == Idle) {
    return;
  }
  switch (event_type) {
  case ARDUINO_EVENT_WIFI_SCAN_DONE:
    onScanDone(event_info.wifi_scan_done);
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    memcpy(ssid, event_info.wifi_sta_disconnected.ssid, event_info.wifi_sta_disconnected.ssid_len);
    log_v("[AWM][onEvent] WiFi disconnected: SSID=`%s`, reason=%s", ssid,
      WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(event_info.wifi_sta_disconnected.reason)));
    if(currentAp != foundAPs.end()) currentAp++;
    tryNextAP();
    break;
  case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    log_v("[AWM][onEvent] WiFi lost IP: SSID=%s", WiFi.SSID().c_str());
    if(currentAp != foundAPs.end()) currentAp++;
    tryNextAP();
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    log_i("[AWM][onEvent] WiFi connected: SSID=`%s`, IP=%s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    _status = Connected;
    if(onConnectedCallback) {
      onConnectedCallback(*currentAp);
    }
    break;
  default:
    break;
  }
}

void AsyncWiFiMulti::onScanDone(const wifi_event_sta_scan_done_t &scanInfo) {
  log_v("[AWM][onScanDone] Scan done: status=%d, number=%d, scan_id=%d", scanInfo.status, scanInfo.number, scanInfo.scan_id);
  if(scanInfo.status != 0) {
    log_i("[AWM][onScanDone] Scan failed with status %d", scanInfo.status);
    onFailure();
    return;
  }
  
  ApSettings::List allFoundAPs;
  for(int i = 0; i < scanInfo.number; ++i) {
    
    String  scan_ssid;
    int32_t scan_rssi;
    uint8_t scan_sec;
    uint8_t *scan_bssid;
    int32_t scan_chan;
    bool    scan_hidden;
    
    WiFi.getNetworkInfo(i, scan_ssid, scan_sec, scan_rssi, scan_bssid, scan_chan);

    ApSettings found;
    found.ssid = {scan_ssid.c_str()};
    found.rssi = scan_rssi;
    memcpy(&found.bssid, scan_bssid, 6);
    found.channel = scan_chan;
    #if DEBUG_WIFI_MULTI
    log_v("[AWM][onScanDone] Found AP: " _MAC_ " %s, RSSI: %d, channel: %i", 
      found.bssid[0], found.bssid[1], found.bssid[2], found.bssid[3],found.bssid[4],found.bssid[5],
      found.ssid.c_str(), found.rssi, found.channel);
    #endif
    allFoundAPs.push_front(found);
  }
  
  // Sort scan results by RSSI
  allFoundAPs.sort([](const ApSettings &a, const ApSettings &b) {
    return a.rssi > b.rssi;
  });

  // match with found APs
  foundAPs.clear();
  for(auto &ap : allFoundAPs) {
    for(auto &configured : configuredAPs) {
      if(configured.ssid == ap.ssid) {
        ap.passphrase = configured.passphrase;
        foundAPs.push_back(ap);
        break;
      } 
    }
  }

  #if DEBUG_WIFI_MULTI
  log_d("[AWM][onScanDone] Sorted APs by RSSI:");
  for(const auto &ap : allFoundAPs) {
    log_d("[AWM][onScanDone] Scan AP: " _MAC_ " %s %ddB", 
      ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
      ap.ssid.c_str(), 
      ap.rssi);
  }
  log_d("[AWM][onScanDone] Found APs by RSSI:");
  for(const auto &ap : foundAPs) {
    log_d("[AWM][onScanDone] Found configured AP: " _MAC_ " %s %ddB channel: %i passphrase: %s", 
      ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
      ap.ssid.c_str(), 
      ap.rssi, ap.channel, ap.passphrase.c_str());
  }
  #endif
  currentAp = foundAPs.begin();
  tryNextAP();
}

void AsyncWiFiMulti::onFailure() {
  log_i("[AWM][onFailure] Failed to connect to any configured APs");
  _status = Idle;
  if(onFailureCallback) {
    onFailureCallback();
  }
}

void AsyncWiFiMulti::tryNextAP() {
  if(currentAp != foundAPs.end()) {
    log_v("[AWM][tryNextAP] Trying next AP: " _MAC_ " %s", 
      currentAp->bssid[0], currentAp->bssid[1], currentAp->bssid[2], 
      currentAp->bssid[3], currentAp->bssid[4], currentAp->bssid[5],
      currentAp->ssid.c_str());
    WiFi.begin(currentAp->ssid.c_str(), 
               currentAp->passphrase.c_str(), 
               currentAp->channel,
               currentAp->bssid);
  } else {
    log_v("[AWM][tryNextAP] No more APs to try");
    onFailure();
  }
}

const char *AsyncWiFiMulti::statusString() {
  switch (_status) {
  case Idle:
    return "Idle";
  case Running:
    return "Running";
  case Connected:
    return "Connected";
  default:
    return "Unknown";
  }
}
