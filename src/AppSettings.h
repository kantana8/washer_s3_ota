#pragma once

#include <Arduino.h>
#include <Preferences.h>

struct AppSettings {
  String terminalId = "WASHER001";
  uint8_t pulseCount[3] = {3, 4, 5};
  uint16_t durationMinutes[3] = {25, 25, 25};
  uint8_t brightness = 255;
  uint8_t volume = 100;
  String settingsPin = "1234";
  String wifiSsid = "UNITEL_Kan_plus";
  String wifiPassword = "Kan898998";
  String wifiSsid2 = "Elephant Wash & Dry";
  String wifiPassword2 = "Elephant_wash";
  bool wifiEnabled1 = true;
  bool wifiEnabled2 = true;

  void load();
  void save() const;
};
