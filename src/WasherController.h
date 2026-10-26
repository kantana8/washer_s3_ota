#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "AppSettings.h"
#include "WasherView.h"

class WasherController : public WasherViewListener {
 public:
  WasherController(AppSettings& settings, WasherView& view);
  void begin();
  void update();

  void onOptionSelected(uint8_t option) override;
  void onPricePageOpened() override;
  void onBackRequested() override;
  void onSettingsSaved() override;
  void onRelayTest(uint8_t option, uint8_t pulseCount) override;
  void onWifiConnectRequested(uint8_t profile, const String& ssid, const String& password) override;
  void onWifiScanStarted() override;
  void onWifiScanEnded() override;
  void beep(uint8_t count) override;

 private:
  static WasherController* instance_;
  static void mqttThunk(char* topic, uint8_t* payload, unsigned int length);

  void handleMqtt(const String& topic, const String& message);
  void reconnectMqtt();
  void subscribeTopics();
  void publish(const String& topic, const String& message, bool retained = false);
  void publishCountdown();
  void publishConfiguration();
  void startWash(uint8_t option);
  void resumeWash(uint8_t option, uint32_t seconds);
  void finishWash();
  void stopWash(bool notifyServer);
  void beginRelayPulses(uint8_t count);
  void serviceWifi();
  void connectWifiProfile(uint8_t profile);
  void saveRuntime();
  void loadRuntime();
  void clearRuntime();
  void handleOtaRequest(const String& payload);
  void performOtaUpdate(const String& url);
  String tidTopic(const String& suffix) const;
  String countdownTopic(uint8_t option) const;

  AppSettings& settings_;
  WasherView& view_;
  WiFiClient wifiClient_;
  PubSubClient mqtt_{wifiClient_};

  uint32_t prices_[3] = {};
  bool optionOffline_[3] = {false, false, false};
  bool isOffline_ = false;
  uint8_t selectedOption_ = 0;
  uint8_t activeOption_ = 0;
  uint32_t remainingSeconds_ = 0;
  uint32_t pageTimeoutAt_ = 0;
  bool qrCountdownStarted_ = false;
  uint32_t runningPreviewUntil_ = 0;
  uint32_t paymentUntil_ = 0;
  uint32_t paymentSuccessUntil_ = 0;
  uint32_t successUntil_ = 0;
  uint32_t nextCountdownAt_ = 0;
  uint32_t nextMqttAttemptAt_ = 0;
  uint32_t nextUiSyncAt_ = 0;

  uint32_t wifiAttemptStartedAt_ = 0;
  uint32_t profileActiveSince_ = 0;
  uint32_t mqttDisconnectedAt_ = 0;
  uint8_t wifiProfile_ = 0;
  uint8_t wifiFailedAttempts_ = 0;
  bool wifiWasConnected_ = false;
  bool isScanningWifi_ = false;
  uint8_t pendingWorkingDoneOption_ = 0;
  uint8_t lastFinishedOption_ = 0;
  bool pendingStartTestAck_ = false;
  uint32_t pendingStartTestTarget_ = 0;
  bool isUpdatingOta_ = false;
};
