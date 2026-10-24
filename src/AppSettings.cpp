#include "AppSettings.h"

void AppSettings::load() {
  Preferences preferences;
  bool hasOwnSettings = preferences.begin("washer", true);
  bool needsSave = !hasOwnSettings;
  if (hasOwnSettings) {
    terminalId = preferences.getString("terminal", terminalId);
    brightness = preferences.getUChar("brightness", brightness);
    volume = preferences.getUChar("volume", volume);
    if (volume > 100) volume = 100;
    if (preferences.isKey("pin")) settingsPin = preferences.getString("pin", settingsPin);
    else needsSave = true;
    if (preferences.isKey("wifi_ssid")) {
      wifiSsid = preferences.getString("wifi_ssid", wifiSsid);
      if (wifiSsid.isEmpty()) { wifiSsid = "UNITEL_Kan_plus"; needsSave = true; }
    } else needsSave = true;
    if (preferences.isKey("wifi_pass")) {
      wifiPassword = preferences.getString("wifi_pass", wifiPassword);
      if (wifiPassword.isEmpty() && wifiSsid == "UNITEL_Kan_plus") { wifiPassword = "Kan898998"; needsSave = true; }
    } else needsSave = true;
    if (preferences.isKey("wifi_ssid2")) {
      wifiSsid2 = preferences.getString("wifi_ssid2", wifiSsid2);
      if (wifiSsid2.isEmpty()) { wifiSsid2 = "Elephant Wash & Dry"; needsSave = true; }
    } else needsSave = true;
    if (preferences.isKey("wifi_pass2")) {
      wifiPassword2 = preferences.getString("wifi_pass2", wifiPassword2);
      if (wifiPassword2.isEmpty() && wifiSsid2 == "Elephant Wash & Dry") { wifiPassword2 = "Elephant_wash"; needsSave = true; }
    } else needsSave = true;
    if (preferences.isKey("wifi_en1")) wifiEnabled1 = preferences.getBool("wifi_en1", wifiEnabled1);
    else needsSave = true;
    if (preferences.isKey("wifi_en2")) wifiEnabled2 = preferences.getBool("wifi_en2", wifiEnabled2);
    else needsSave = true;
    for (uint8_t i = 0; i < 3; ++i) {
      const String pulseKey = "pulse" + String(i);
      const String timeKey = "time" + String(i);
      pulseCount[i] = preferences.getUChar(pulseKey.c_str(), pulseCount[i]);
      durationMinutes[i] = preferences.getUShort(timeKey.c_str(), durationMinutes[i]);
    }
  }
  preferences.end();

  if (!hasOwnSettings || terminalId == "WASHER001") {
    Preferences legacy;
    if (legacy.begin("storage", true)) {
      const String legacyTerminal = legacy.getString("TERMINAL", "");
      if (!legacyTerminal.isEmpty()) terminalId = legacyTerminal;
      legacy.end();
    }
    needsSave = true;
  }
  if (needsSave) save();
}

void AppSettings::save() const {
  Preferences preferences;
  preferences.begin("washer", false);
  preferences.putString("terminal", terminalId);
  preferences.putUChar("brightness", brightness);
  preferences.putUChar("volume", volume);
  preferences.putString("pin", settingsPin);
  preferences.putString("wifi_ssid", wifiSsid);
  preferences.putString("wifi_pass", wifiPassword);
  preferences.putString("wifi_ssid2", wifiSsid2);
  preferences.putString("wifi_pass2", wifiPassword2);
  preferences.putBool("wifi_en1", wifiEnabled1);
  preferences.putBool("wifi_en2", wifiEnabled2);
  for (uint8_t i = 0; i < 3; ++i) {
    const String pulseKey = "pulse" + String(i);
    const String timeKey = "time" + String(i);
    preferences.putUChar(pulseKey.c_str(), pulseCount[i]);
    preferences.putUShort(timeKey.c_str(), durationMinutes[i]);
  }
  preferences.end();
}
