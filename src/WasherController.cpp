#include "WasherController.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "AppConfig.h"

namespace {

struct RtcWashState {
  uint32_t magic;
  uint8_t activeOption;
  uint32_t remainingSeconds;
};

constexpr uint32_t kRtcWashMagic = 0x57415348U; // 'WASH'
constexpr char kFirmwareVersion[] = "3.5-v1.8";
RTC_NOINIT_ATTR RtcWashState s_rtcWashState;

QueueHandle_t relayQueue = nullptr;
volatile uint32_t s_relayCompletedCount = 0;

void relayTaskFunc(void* /*param*/) {
  uint8_t count = 0;
  for (;;) {
    if (xQueueReceive(relayQueue, &count, portMAX_DELAY) == pdTRUE) {
      if (count == 0) continue;
      Serial.printf("[RELAY] Firing %u hardware-precise pulses (80ms HIGH / 120ms LOW)\n", count);
      for (uint8_t i = 0; i < count; ++i) {
        digitalWrite(config::RelayPin, HIGH);
        vTaskDelay(pdMS_TO_TICKS(80)); // 80ms HIGH (spec: 10ms - 160ms)
        digitalWrite(config::RelayPin, LOW);
        vTaskDelay(pdMS_TO_TICKS(120)); // 120ms LOW (total period: 200ms >= 180ms min spec)
      }
      digitalWrite(config::RelayPin, LOW);
      s_relayCompletedCount += count;
      Serial.printf("[RELAY] %u pulses completed\n", count);
    }
  }
}

} // namespace

WasherController* WasherController::instance_ = nullptr;

WasherController::WasherController(AppSettings& settings, WasherView& view)
    : settings_(settings), view_(view) {}

void WasherController::begin() {
  instance_ = this;
  pinMode(config::RelayPin, OUTPUT);
  digitalWrite(config::RelayPin, LOW);

  pinMode(config::BuzzerPin, INPUT);
  ledcSetup(6, 2200, 8);

  if (!relayQueue) {
    relayQueue = xQueueCreate(10, sizeof(uint8_t));
    xTaskCreatePinnedToCore(relayTaskFunc, "relayTask", 2048, nullptr, 5, nullptr, 1);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  uint8_t initProfile = 0;
  if (!settings_.wifiEnabled1 && settings_.wifiEnabled2) initProfile = 1;
  if (settings_.wifiEnabled1 || settings_.wifiEnabled2) {
    connectWifiProfile(initProfile);
  } else {
    WiFi.disconnect(true, true);
    view_.setWifiStatus("WiFi: DISABLED");
  }
  configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");

  wifiClient_.setTimeout(2);
  mqtt_.setServer(settings_.mqttHost.c_str(), settings_.mqttPort);
  mqtt_.setBufferSize(1024);
  mqtt_.setKeepAlive(30);
  mqtt_.setSocketTimeout(2);
  mqtt_.setCallback(mqttThunk);
  loadRuntime();
  if (activeOption_ && remainingSeconds_ > 0) {
    Serial.printf("[RESTORE] Resuming wash after power loss: option %u, %lu s remaining\n",
                  activeOption_, static_cast<unsigned long>(remainingSeconds_));
    resumeWash(activeOption_, remainingSeconds_);
  } else {
    clearRuntime();
    view_.show(Page::Home);
  }
}

void WasherController::update() {
  if (isUpdatingOta_) return;
  view_.update();
  serviceWifi();

  const uint32_t now = millis();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected) {
    if (!mqtt_.connected() && now >= nextMqttAttemptAt_) reconnectMqtt();
    mqtt_.loop();
  }
  const bool mqttConnected = mqtt_.connected();
  static bool lastSyncWifi = false;
  static bool lastSyncMqtt = false;
  if (now >= nextUiSyncAt_ || wifiConnected != lastSyncWifi || mqttConnected != lastSyncMqtt) {
    nextUiSyncAt_ = now + 500;
    lastSyncWifi = wifiConnected;
    lastSyncMqtt = mqttConnected;
    view_.setConnection(wifiConnected, mqttConnected, wifiConnected ? WiFi.RSSI() : -100);
    view_.setActiveWifi(wifiProfile_, wifiConnected, wifiConnected ? WiFi.SSID() : "", wifiConnected ? WiFi.localIP().toString() : "");
  }

  // Connection Watchdog: show Page::Pause if MQTT disconnects for more than 30 seconds while idle
  if (wifiConnected && mqttConnected) {
    mqttDisconnectedAt_ = 0;
    // Auto-deliver pending wash completion if connection was restored
    if (pendingWorkingDoneOption_) {
      publish("tid/status", settings_.terminalId + "/working_done/option" + String(pendingWorkingDoneOption_));
      publish(countdownTopic(pendingWorkingDoneOption_), "{\"current_page\":6,\"countdown\":0}", true);
      pendingWorkingDoneOption_ = 0;
    }
    if (pendingStartTestAck_ && static_cast<int32_t>(s_relayCompletedCount - pendingStartTestTarget_) >= 0) {
      pendingStartTestAck_ = false;
      publish("tid/status", settings_.terminalId + "/start_test_ok/1_pulse");
    }
    // Auto-Recovery: return to Home page when connection is restored if currently on Pause page, but NOT if server set it offline
    if (!activeOption_ && view_.page() == Page::Pause && !isOffline_ && !optionOffline_[0]) {
      Serial.println("[Watchdog] Connection restored; auto-recovering to Page::Home");
      view_.show(Page::Home);
    }
  } else {
    if (activeOption_) {
      // Don't accumulate watchdog time during active washing
      mqttDisconnectedAt_ = now;
    } else if (mqttDisconnectedAt_ == 0) {
      mqttDisconnectedAt_ = now;
    }
  }
  if (!isScanningWifi_ && mqttDisconnectedAt_ && (now - mqttDisconnectedAt_ > 30000) && !activeOption_ && 
      view_.page() != Page::Pause && view_.page() != Page::Pin && view_.page() != Page::Settings && view_.page() != Page::WifiScan) {
    Serial.println("[Watchdog] Disconnected >30s; showing Page::Pause");
    view_.setPauseText("ຢຸດໃຫ້ບໍລິການຊົ່ວຄາວ");
    view_.show(Page::Pause);
  }

  if (pageTimeoutAt_ && !activeOption_) {
    if (view_.page() == Page::QrCode && view_.isQrLoaded()) {
      if (!qrCountdownStarted_) {
        qrCountdownStarted_ = true;
        pageTimeoutAt_ = now + config::QrScanTimeoutMs;
      }
      const uint32_t remSec = (pageTimeoutAt_ > now) ? ((pageTimeoutAt_ - now + 999U) / 1000U) : 0U;
      view_.setQrTimeoutSeconds(remSec);
    }
    if (now >= pageTimeoutAt_) {
      pageTimeoutAt_ = 0;
      qrCountdownStarted_ = false;
      if (view_.page() != Page::Settings && view_.page() != Page::Pin && view_.page() != Page::WifiScan) {
        view_.show(Page::Home);
      }
    }
  }
  if (activeOption_ && paymentUntil_ && now >= paymentUntil_) {
    paymentUntil_ = 0;
    paymentSuccessUntil_ = now + config::PaymentSuccessMs;
    view_.show(Page::PaymentSuccess);
  }
  if (activeOption_ && !paymentUntil_ && paymentSuccessUntil_ && now >= paymentSuccessUntil_) {
    paymentSuccessUntil_ = 0;
    runningPreviewUntil_ = now + 5000U;
    view_.show(Page::Running);
    view_.setCountdown(remainingSeconds_, activeOption_);
    nextCountdownAt_ = now + 1000;
  }
  if (activeOption_ && runningPreviewUntil_ && now >= runningPreviewUntil_) {
    runningPreviewUntil_ = 0;
    if (view_.page() == Page::Running) {
      view_.show(Page::Home);
    }
  }
  if (activeOption_ && !paymentUntil_ && !paymentSuccessUntil_ && now >= nextCountdownAt_) {
    nextCountdownAt_ += 1000;
    if (remainingSeconds_ > 0) --remainingSeconds_;
    view_.setCountdown(remainingSeconds_, activeOption_);
    // Update zero-wait RTC_NOINIT_ATTR state every 1s (survives brownout/soft reset with 1s accuracy)
    s_rtcWashState.magic = kRtcWashMagic;
    s_rtcWashState.activeOption = activeOption_;
    s_rtcWashState.remainingSeconds = remainingSeconds_;
    // Persist to NVS Flash every 15s (survives full cold power outage without wearing Flash or spamming MQTT)
    if ((remainingSeconds_ % 15 == 0) || (remainingSeconds_ == 0)) {
      saveRuntime();
    }
    if (remainingSeconds_ == 0) finishWash();
  }
  if (successUntil_ && now >= successUntil_) {
    successUntil_ = 0;
    view_.hideResetPopup();
    view_.show(Page::Home);
    if (lastFinishedOption_) {
      publish(countdownTopic(lastFinishedOption_), "{\"current_page\":0,\"countdown\":0}", true);
      lastFinishedOption_ = 0;
    }
  }
}

void WasherController::onOptionSelected(uint8_t option) {
  if (option < 1 || option > 3) return;
  if (activeOption_) {
    runningPreviewUntil_ = millis() + 5000U;
    view_.show(Page::Running);
    view_.setCountdown(remainingSeconds_, activeOption_);
    return;
  }
  if (optionOffline_[option - 1] || prices_[option - 1] == 0) {
    Serial.printf("[FLOW] Option %u is offline or price is 0, cannot select\n", option);
    beep(1);
    return;
  }
  selectedOption_ = option;
  qrCountdownStarted_ = false;
  view_.setPrice(option, prices_[option - 1]);
  pageTimeoutAt_ = millis() + config::PageTimeoutMs;
  if (settings_.gatewayBcelMode) {
    view_.setSelectedBank("lapnet");
    view_.show(Page::QrCode);
    view_.setQrOption(option);
    publish("tid/qrcode/option" + String(option), settings_.terminalId);
  } else {
    view_.setSelectBankOption(option);
    view_.show(Page::SelectBank);
  }
}

void WasherController::onBankSelected(const char* bankCode) {
  if (!selectedOption_ || selectedOption_ > 3 || !bankCode) return;
  qrCountdownStarted_ = false;
  view_.setSelectedBank(bankCode);
  view_.show(Page::QrCode);
  view_.setQrOption(selectedOption_);
  view_.setPrice(selectedOption_, prices_[selectedOption_ - 1]);
  pageTimeoutAt_ = millis() + config::PageTimeoutMs;
  const uint32_t rnd = (esp_random() % 9000U) + 1000U;
  const String billId = settings_.terminalId + "-B" + String(selectedOption_) + String(rnd);
  const String payload = settings_.terminalId + "," + String(bankCode) + "," + String(prices_[selectedOption_ - 1]) + "," + billId;
  publish("tid/qrcode/option" + String(selectedOption_), payload);
}

void WasherController::onPricePageOpened() {
  if (activeOption_) {
    runningPreviewUntil_ = millis() + 5000U;
    view_.show(Page::Running);
    view_.setCountdown(remainingSeconds_, activeOption_);
    return;
  }
  view_.show(Page::Select);
  pageTimeoutAt_ = millis() + config::PageTimeoutMs;
  if (prices_[0] == 0 && prices_[1] == 0 && prices_[2] == 0 && !optionOffline_[0]) {
    for (uint8_t option = 1; option <= 3; ++option) {
      publish("tid/price/option" + String(option), settings_.terminalId);
    }
  }
}

void WasherController::onBackRequested() {
  qrCountdownStarted_ = false;
  if (view_.page() == Page::QrCode) {
    if (settings_.gatewayBcelMode) {
      view_.show(Page::Select);
    } else {
      view_.setSelectBankOption(selectedOption_);
      view_.show(Page::SelectBank);
    }
    pageTimeoutAt_ = millis() + config::PageTimeoutMs;
  } else if (view_.page() == Page::SelectBank) {
    view_.show(Page::Select);
    pageTimeoutAt_ = millis() + config::PageTimeoutMs;
  } else {
    pageTimeoutAt_ = 0;
    view_.show(Page::Home);
  }
}

void WasherController::onSettingsSaved() {
  mqtt_.disconnect();
  mqtt_.setServer(settings_.mqttHost.c_str(), settings_.mqttPort);
  nextMqttAttemptAt_ = 0;
}

void WasherController::onRelayTest(uint8_t option, uint8_t pulseCount) {
  (void)option;
  beginRelayPulses(pulseCount);
}

void WasherController::onWifiConnectRequested(uint8_t profile, const String&, const String&) {
  mqtt_.disconnect();
  wifiFailedAttempts_ = 0;
  connectWifiProfile(profile > 1 ? 0 : profile);
  nextMqttAttemptAt_ = 0;
}

void WasherController::onWifiScanStarted() {
  isScanningWifi_ = true;
  mqtt_.disconnect();
  WiFi.disconnect(false, false);
}

void WasherController::onWifiScanEnded() {
  isScanningWifi_ = false;
  wifiAttemptStartedAt_ = millis();
  connectWifiProfile(wifiProfile_);
}

void WasherController::mqttThunk(char* topic, uint8_t* payload, unsigned int length) {
  if (!instance_) return;
  const String topicStr(topic);
  String message;
  message.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) message += static_cast<char>(payload[i]);
  instance_->handleMqtt(topicStr, message);
}

void WasherController::handleMqtt(const String& topic, const String& message) {
  Serial.printf("[MQTT] %s -> %s\n", topic.c_str(), message.c_str());
  if (topic == tidTopic("/qrcode/res") || topic == settings_.terminalId + "/qrcode/res" ||
      topic == tidTopic("/qrcode") || topic == settings_.terminalId + "/qrcode") {
    Serial.printf("[QR] Received QR payload: len=%u\n", message.length());
    view_.setQrCode(message);
    return;
  }

  for (uint8_t option = 1; option <= 3; ++option) {
    if (topic == tidTopic("/price/option" + String(option)) ||
        topic == settings_.terminalId + "/price/option" + String(option)) {
      if (message.equalsIgnoreCase("offline")) {
        prices_[option - 1] = 0;
        optionOffline_[option - 1] = true;
        Serial.printf("[PRICE] Option %u is OFFLINE\n", option);
        if (option == 1 || (optionOffline_[0] && optionOffline_[1] && optionOffline_[2])) {
          isOffline_ = true;
        }
        if (!activeOption_ && isOffline_) {
          if (view_.page() != Page::Settings && view_.page() != Page::Pin && view_.page() != Page::WifiScan) {
            view_.setPauseText("ຢຸດໃຫ້ບໍລິການຊົ່ວຄາວ");
            view_.show(Page::Pause);
          }
        }
        return;
      }
      optionOffline_[option - 1] = false;
      prices_[option - 1] = message.toInt();
      view_.setPrice(option, prices_[option - 1]);
      if (option == 1 && prices_[0] > 0) {
        isOffline_ = false;
      }
      if (!activeOption_ && view_.page() == Page::Pause && !isOffline_) {
        Serial.println("[PRICE] Valid price received; returning to Home page");
        view_.show(Page::Home);
      }
      return;
    }
    if (topic == tidTopic("/start/option" + String(option)) ||
        topic == settings_.terminalId + "/start/option" + String(option)) {
      if (message == "onepay/option" + String(option) || message == "web/option" + String(option)) {
        startWash(option);
      }
      return;
    }
  }

  if (topic == tidTopic("/status") || topic == settings_.terminalId + "/status") {
    if (message == "online" && !activeOption_) {
      isOffline_ = false;
      optionOffline_[0] = optionOffline_[1] = optionOffline_[2] = false;
      if (view_.page() != Page::Settings && view_.page() != Page::Pin && view_.page() != Page::WifiScan) {
        view_.show(Page::Home);
      }
    }
    else if (message == "offline" && !activeOption_) {
      isOffline_ = true;
      optionOffline_[0] = optionOffline_[1] = optionOffline_[2] = true;
      if (view_.page() != Page::Settings && view_.page() != Page::Pin && view_.page() != Page::WifiScan) {
        view_.setPauseText("ຢຸດໃຫ້ບໍລິການຊົ່ວຄາວ");
        view_.show(Page::Pause);
      }
    }
    else if (message == "connect_test") {
      const int32_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
      publish("tid/status", settings_.terminalId + "/connected/" + String(rssi) + "/" + kFirmwareVersion);
      if (activeOption_) {
        publishCountdown();
      }
    }
    else if (message == "stop") stopWash(true);
    return;
  }
  if ((topic == tidTopic("/start_test") || topic == settings_.terminalId + "/start_test") && message == "click") {
    pendingStartTestTarget_ = s_relayCompletedCount + 1;
    pendingStartTestAck_ = true;
    beginRelayPulses(1);
    beep(1);
    return;
  }
  if (topic == settings_.terminalId + "/pulse/statas" || topic == settings_.terminalId + "/pulse/status") {
    publishConfiguration();
  }
  if (topic == tidTopic("/gateway") || topic == settings_.terminalId + "/gateway") {
    String gw = message;
    gw.trim();
    gw.toLowerCase();
    const bool bcelMode = (gw != "phajay");
    if (settings_.gatewayBcelMode != bcelMode) {
      settings_.saveGatewayMode(bcelMode);
      Serial.printf("[GATEWAY] Updated gateway mode to %s\n", bcelMode ? "BCEL (Direct QR)" : "Phajay (Select Bank)");
    }
    if (bcelMode && view_.page() == Page::SelectBank) {
      view_.show(Page::Select);
    }
    return;
  }
  if (topic == tidTopic("/ota") || topic == settings_.terminalId + "/ota") {
    handleOtaRequest(message);
    return;
  }
}

void WasherController::reconnectMqtt() {
  nextMqttAttemptAt_ = millis() + 5000;
  const String clientId = "WASHER_" + settings_.terminalId + "_" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
  mqtt_.setServer(settings_.mqttHost.c_str(), settings_.mqttPort);
  Serial.printf("[MQTT] connecting to %s:%u (user=%s)\n", settings_.mqttHost.c_str(), settings_.mqttPort, settings_.mqttUser.c_str());
  const String willMsg = settings_.terminalId + "/disconnected";
  bool connected = settings_.mqttUser.isEmpty()
      ? mqtt_.connect(clientId.c_str(), nullptr, nullptr, "tid/status", 0, false, willMsg.c_str())
      : mqtt_.connect(clientId.c_str(), settings_.mqttUser.c_str(), settings_.mqttPassword.c_str(), "tid/status", 0, false, willMsg.c_str());
  if (!connected) return;
  subscribeTopics();
  const int32_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  publish("tid/status", settings_.terminalId + "/check/" + String(rssi) + "/" + kFirmwareVersion);
  publishConfiguration();

  if (pendingWorkingDoneOption_) {
    publish("tid/status", settings_.terminalId + "/working_done/option" + String(pendingWorkingDoneOption_));
    publish(countdownTopic(pendingWorkingDoneOption_), "{\"current_page\":6,\"countdown\":0}", true);
    pendingWorkingDoneOption_ = 0;
  }

  if (activeOption_) {
    publish("tid/status", settings_.terminalId + "/started/option" + String(activeOption_));
    publishCountdown();
  } else {
    for (uint8_t option = 1; option <= 3; ++option) {
      publish(countdownTopic(option), "{\"current_page\":0,\"countdown\":0}", true);
    }
  }
}

void WasherController::subscribeTopics() {
  mqtt_.subscribe(tidTopic("/#").c_str());
  mqtt_.subscribe((settings_.terminalId + "/gateway").c_str());
  mqtt_.subscribe((settings_.terminalId + "/start_test").c_str());
  mqtt_.subscribe((settings_.terminalId + "/ota").c_str());
  mqtt_.subscribe((settings_.terminalId + "/pulse/statas").c_str());
  mqtt_.subscribe((settings_.terminalId + "/pulse/status").c_str());
}

void WasherController::publish(const String& topic, const String& message, bool retained) {
  if (!mqtt_.connected()) return;
  mqtt_.publish(topic.c_str(), message.c_str(), retained);
  Serial.printf("[MQTT] publish %s -> %s\n", topic.c_str(), message.c_str());
}

void WasherController::publishConfiguration() {
  for (uint8_t i = 0; i < 3; ++i) {
    publish(settings_.terminalId + "/pulse/value" + String(i + 1), String(settings_.pulseCount[i]), true);
  }
}

void WasherController::startWash(uint8_t option) {
  if (option < 1 || option > 3) return;
  if (activeOption_) {
    publish("tid/status", settings_.terminalId + "/started/option" + String(activeOption_));
    publishCountdown();
    return;
  }
  pageTimeoutAt_ = 0;
  selectedOption_ = option;
  activeOption_ = option;
  const uint8_t pulseCount = settings_.pulseCount[option - 1];
  remainingSeconds_ = static_cast<uint32_t>(settings_.durationMinutes[option - 1]) * 60U;
  beginRelayPulses(pulseCount);
  beep(2);
  view_.show(Page::Payment);
  Serial.printf("[FLOW] payment page requested (pulses=%u)\n", pulseCount);
  const uint32_t transferDurationMs = static_cast<uint32_t>(pulseCount) * 200U + 800U;
  paymentUntil_ = millis() + max(transferDurationMs, config::CreditTransferMs);
  paymentSuccessUntil_ = 0;
  runningPreviewUntil_ = 0;
  publish("tid/status", settings_.terminalId + "/started/option" + String(option));
  publishCountdown();
  saveRuntime();
  Serial.println("[FLOW] startWash complete");
}

void WasherController::resumeWash(uint8_t option, uint32_t seconds) {
  activeOption_ = option;
  remainingSeconds_ = seconds;
  paymentUntil_ = 0;
  paymentSuccessUntil_ = 0;
  runningPreviewUntil_ = millis() + 5000U;
  successUntil_ = 0;
  view_.show(Page::Running);
  view_.setCountdown(seconds, option);
  nextCountdownAt_ = millis() + 1000;
  beep(2);
  saveRuntime();
}

void WasherController::finishWash() {
  const uint8_t finishedOption = activeOption_;
  runningPreviewUntil_ = 0;
  view_.show(Page::Success);
  pendingWorkingDoneOption_ = finishedOption;
  lastFinishedOption_ = finishedOption;
  publishCountdown();
  if (mqtt_.connected()) {
    publish("tid/status", settings_.terminalId + "/working_done/option" + String(finishedOption));
    publish(countdownTopic(finishedOption), "{\"current_page\":6,\"countdown\":0}", true);
    pendingWorkingDoneOption_ = 0;
  }
  activeOption_ = 0;
  clearRuntime();
  beep(3);
  mqttDisconnectedAt_ = millis();
  successUntil_ = millis() + config::SuccessDelayMs;
}

void WasherController::stopWash(bool notifyServer) {
  activeOption_ = 0;
  remainingSeconds_ = 0;
  paymentUntil_ = 0;
  paymentSuccessUntil_ = 0;
  runningPreviewUntil_ = 0;
  successUntil_ = 0;
  lastFinishedOption_ = 0;
  digitalWrite(config::RelayPin, LOW);
  if (relayQueue) xQueueReset(relayQueue);
  clearRuntime();
  for (uint8_t option = 1; option <= 3; ++option) {
    publish(countdownTopic(option), "{\"current_page\":0,\"countdown\":0}", true);
  }
  if (notifyServer) publish("tid/status", settings_.terminalId + "/stopped");
  beep(1);
  view_.showResetPopup();
  successUntil_ = millis() + 2000;
}

void WasherController::publishCountdown() {
  if (!activeOption_) return;
  char json[64];
  snprintf(json, sizeof(json), "{\"current_page\":%u,\"countdown\":%lu}",
           remainingSeconds_ ? 5 : 6, static_cast<unsigned long>(remainingSeconds_));
  publish(countdownTopic(activeOption_), json, true);
}

void WasherController::beginRelayPulses(uint8_t count) {
  if (!count || !relayQueue) return;
  xQueueSend(relayQueue, &count, 0);
}

void WasherController::connectWifiProfile(uint8_t profile) {
  if (!settings_.wifiEnabled1 && !settings_.wifiEnabled2) {
    WiFi.disconnect(true, true);
    view_.setWifiStatus("WiFi: DISABLED");
    return;
  }
  
  if (profile == 0 && !settings_.wifiEnabled1 && settings_.wifiEnabled2) profile = 1;
  else if (profile == 1 && !settings_.wifiEnabled2 && settings_.wifiEnabled1) profile = 0;

  wifiProfile_ = profile > 1 ? 0 : profile;
  wifiWasConnected_ = false;
  wifiAttemptStartedAt_ = millis();
  profileActiveSince_ = millis();
  mqttDisconnectedAt_ = millis();
  WiFi.disconnect(false, false);
  delay(30);

  const String& ssid = wifiProfile_ ? settings_.wifiSsid2 : settings_.wifiSsid;
  const String& password = wifiProfile_ ? settings_.wifiPassword2 : settings_.wifiPassword;
  Serial.printf("[WiFi] trying profile %u: %s\n", wifiProfile_ + 1,
                ssid.isEmpty() ? "stored ESP32 credentials" : ssid.c_str());
  if (ssid.isEmpty()) WiFi.begin();
  else WiFi.begin(ssid.c_str(), password.c_str());
}

void WasherController::serviceWifi() {
  if (isScanningWifi_) return;
  if (!settings_.wifiEnabled1 && !settings_.wifiEnabled2) return;

  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected_) {
      wifiWasConnected_ = true;
      wifiFailedAttempts_ = 0;
      profileActiveSince_ = now;
      const String connectedSsid = WiFi.SSID();
      const String connectedPassword = WiFi.psk();
      String& savedSsid = wifiProfile_ ? settings_.wifiSsid2 : settings_.wifiSsid;
      String& savedPassword = wifiProfile_ ? settings_.wifiPassword2 : settings_.wifiPassword;
      if (savedSsid.isEmpty()) {
        savedSsid = connectedSsid;
        savedPassword = connectedPassword;
        settings_.save();
        view_.setWifiProfiles(settings_.wifiSsid, settings_.wifiPassword,
                              settings_.wifiSsid2, settings_.wifiPassword2);
      }
      view_.setWifiStatus("ONLINE: " + connectedSsid);
      Serial.printf("[WiFi] connected with profile %u: %s\n", wifiProfile_ + 1, connectedSsid.c_str());
    }

    // Auto-fallback: if connected to backup WiFi (profile 1) for >15 minutes while idle, attempt to reconnect to primary WiFi (profile 0)
    if (wifiProfile_ == 1 && settings_.wifiEnabled1 && !activeOption_ && !settings_.wifiSsid.isEmpty() && (now - profileActiveSince_ >= 15 * 60 * 1000UL)) {
      Serial.println("[WiFi] 15 mins on backup profile 2; attempting auto-fallback to primary profile 1");
      connectWifiProfile(0);
      return;
    }

    // Internet Loss Failover: If WiFi is connected but MQTT fails to connect for >60 seconds while idle, switch profile
    if (!mqtt_.connected() && mqttDisconnectedAt_ && (now - mqttDisconnectedAt_ >= 60000UL) && !activeOption_) {
      uint8_t nextProfile = wifiProfile_ ? 0 : 1;
      bool nextEnabled = nextProfile ? settings_.wifiEnabled2 : settings_.wifiEnabled1;
      const String& nextSsid = nextProfile ? settings_.wifiSsid2 : settings_.wifiSsid;
      if (nextEnabled && (!nextSsid.isEmpty() || nextProfile == 0)) {
        Serial.printf("[WiFi] MQTT disconnected for >60s (Internet Down); failing over to profile %u\n", nextProfile + 1);
        mqttDisconnectedAt_ = 0;
        connectWifiProfile(nextProfile);
        return;
      }
    }
    return;
  }

  if (wifiWasConnected_) {
    wifiWasConnected_ = false;
    wifiAttemptStartedAt_ = now;
  }
  if (now - wifiAttemptStartedAt_ < 30000) return;

  ++wifiFailedAttempts_;
  if (wifiFailedAttempts_ >= 10) {
    Serial.println("[WiFi] ten profile rounds failed; restarting");
    delay(100);
    ESP.restart();
  }

  uint8_t nextProfile = wifiProfile_ ? 0 : 1;
  if (nextProfile == 1 && !settings_.wifiEnabled2) nextProfile = 0;
  if (nextProfile == 0 && !settings_.wifiEnabled1) nextProfile = 1;
  const String& nextSsid = nextProfile ? settings_.wifiSsid2 : settings_.wifiSsid;
  if (nextSsid.isEmpty()) nextProfile = 0;
  connectWifiProfile(nextProfile);
}

void WasherController::beep(uint8_t count) {
  if (count == 0 || settings_.volume == 0) return;
  pinMode(config::BuzzerPin, OUTPUT);
  ledcAttachPin(config::BuzzerPin, 6);
  const uint8_t duty = static_cast<uint8_t>((static_cast<uint16_t>(settings_.volume) * 128U) / 100U);
  for (uint8_t i = 0; i < count; ++i) {
    ledcWriteTone(6, 2400);
    ledcWrite(6, duty);
    if (washerui::App.speakerAvailable()) {
      washerui::App.playSpeakerBeep(2400, 100);
    } else {
      delay(100); // 100ms beep duration for clear tone
    }
    ledcWrite(6, 0);
    if (i + 1 < count) delay(100); // 100ms pause between beeps
  }
  ledcWrite(6, 0);
  ledcDetachPin(config::BuzzerPin);
  pinMode(config::BuzzerPin, INPUT);
}

void WasherController::saveRuntime() {
  s_rtcWashState.magic = kRtcWashMagic;
  s_rtcWashState.activeOption = activeOption_;
  s_rtcWashState.remainingSeconds = remainingSeconds_;
  Preferences preferences;
  preferences.begin("washer-run", false);
  preferences.putUChar("option", activeOption_);
  preferences.putUInt("remaining", remainingSeconds_);
  preferences.end();
}

void WasherController::loadRuntime() {
  Preferences preferences;
  preferences.begin("washer-run", true);
  const uint8_t nvsOption = preferences.getUChar("option", 0);
  const uint32_t nvsRemaining = preferences.getUInt("remaining", 0);
  preferences.end();

  if (s_rtcWashState.magic == kRtcWashMagic &&
      s_rtcWashState.activeOption >= 1 && s_rtcWashState.activeOption <= 3 &&
      s_rtcWashState.remainingSeconds > 0 &&
      (nvsOption == 0 || (s_rtcWashState.activeOption == nvsOption && s_rtcWashState.remainingSeconds <= nvsRemaining + 15U))) {
    activeOption_ = s_rtcWashState.activeOption;
    remainingSeconds_ = s_rtcWashState.remainingSeconds;
    Serial.printf("[RESTORE] Loaded 1s-accurate state from RTC_NOINIT: opt=%u, rem=%lu s\n",
                  activeOption_, static_cast<unsigned long>(remainingSeconds_));
  } else {
    activeOption_ = nvsOption;
    remainingSeconds_ = nvsRemaining;
    s_rtcWashState.magic = activeOption_ ? kRtcWashMagic : 0;
    s_rtcWashState.activeOption = activeOption_;
    s_rtcWashState.remainingSeconds = remainingSeconds_;
  }
}

void WasherController::clearRuntime() {
  s_rtcWashState.magic = 0;
  s_rtcWashState.activeOption = 0;
  s_rtcWashState.remainingSeconds = 0;
  Preferences preferences;
  preferences.begin("washer-run", false);
  preferences.clear();
  preferences.end();
}

String WasherController::tidTopic(const String& suffix) const {
  return "tid/" + settings_.terminalId + suffix;
}

String WasherController::countdownTopic(uint8_t option) const {
  return settings_.terminalId + "/countdown" + String(option);
}

void WasherController::handleOtaRequest(const String& payload) {
  if (activeOption_ || view_.page() == Page::QrCode || view_.page() == Page::Payment) {
    Serial.println("[OTA] Machine is currently busy. OTA rejected.");
    publish(tidTopic("/ota/status"), "busy: machine in transaction");
    return;
  }
  if (isUpdatingOta_) {
    Serial.println("[OTA] OTA is already in progress.");
    return;
  }

  String downloadUrl = "";
  String trimmed = payload;
  trimmed.trim();

  if (trimmed.startsWith("http://") || trimmed.startsWith("https://")) {
    downloadUrl = trimmed;
  } else if (trimmed.startsWith("{")) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, trimmed);
    if (!err && doc["url"].is<const char*>()) {
      downloadUrl = doc["url"].as<String>();
    }
  }

  if (downloadUrl.isEmpty()) {
    Serial.printf("[OTA] Invalid OTA payload: %s\n", payload.c_str());
    publish(tidTopic("/ota/status"), "failed: invalid url or payload");
    return;
  }

  if (downloadUrl.startsWith("https://")) {
    Serial.println("[OTA] HTTPS is not supported due to partition flash constraints (use HTTP).");
    publish(tidTopic("/ota/status"), "failed: HTTPS not supported (use HTTP)");
    return;
  }

  Serial.printf("[OTA] Triggering OTA update from: %s\n", downloadUrl.c_str());
  performOtaUpdate(downloadUrl);
}

void WasherController::performOtaUpdate(const String& url) {
  isUpdatingOta_ = true;
  publish(tidTopic("/ota/status"), "starting");
  delay(100);

  view_.showOtaProgress(0, "ກຳລັງເລີ່ມຕົ້ນ...");
  washerui::App.update();
  esp_task_wdt_reset();

  Serial.printf("[OTA] Connecting to firmware server: %s\n", url.c_str());

  HTTPClient http;
  WiFiClient client;
  http.begin(client, url);

  http.setTimeout(25000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const char* headerKeys[] = {"Content-Length"};
  http.collectHeaders(headerKeys, 1);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[OTA] HTTP GET failed, error %d: %s\n", httpCode, http.errorToString(httpCode).c_str());
    publish(tidTopic("/ota/status"), "failed: HTTP " + String(httpCode));
    view_.showOtaProgress(0, ("HTTP Error " + String(httpCode)).c_str());
    for (int i = 0; i < 30; ++i) {
      esp_task_wdt_reset();
      washerui::App.update();
      delay(100);
    }
    view_.hideOtaProgress();
    http.end();
    isUpdatingOta_ = false;
    return;
  }

  int contentLength = http.getSize();
  Serial.printf("[OTA] Firmware size: %d bytes\n", contentLength);

  if (contentLength <= 0) {
    Serial.println("[OTA] Content-Length is 0 or negative.");
    publish(tidTopic("/ota/status"), "failed: invalid size");
    view_.showOtaProgress(0, "Invalid size");
    for (int i = 0; i < 30; ++i) {
      esp_task_wdt_reset();
      washerui::App.update();
      delay(100);
    }
    view_.hideOtaProgress();
    http.end();
    isUpdatingOta_ = false;
    return;
  }

  if (contentLength > 6291456) {
    Serial.printf("[OTA] Firmware too large: %d > 6291456 bytes\n", contentLength);
    publish(tidTopic("/ota/status"), "failed: size exceeds 6MB partition");
    view_.showOtaProgress(0, "Firmware too large!");
    for (int i = 0; i < 30; ++i) {
      esp_task_wdt_reset();
      washerui::App.update();
      delay(100);
    }
    view_.hideOtaProgress();
    http.end();
    isUpdatingOta_ = false;
    return;
  }

  if (!Update.begin(contentLength)) {
    Serial.printf("[OTA] Update.begin failed! Error code: %u\n", Update.getError());
    publish(tidTopic("/ota/status"), "failed: Update.begin error " + String(Update.getError()));
    view_.showOtaProgress(0, "OTA Begin Failed");
    for (int i = 0; i < 30; ++i) {
      esp_task_wdt_reset();
      washerui::App.update();
      delay(100);
    }
    view_.hideOtaProgress();
    http.end();
    isUpdatingOta_ = false;
    return;
  }

  publish(tidTopic("/ota/status"), "downloading");

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buff[2048];
  int written = 0;
  int lastPercent = -1;
  uint32_t lastProgressTime = 0;
  uint32_t lastDataAt = millis();

  view_.showOtaProgress(0, "ກຳລັງດາວໂຫຼດ...");
  washerui::App.update();

  while (http.connected() && (written < contentLength)) {
    esp_task_wdt_reset();
    size_t availableBytes = stream->available();
    if (availableBytes > 0) {
      int bytesToRead = min(availableBytes, sizeof(buff));
      int bytesRead = stream->readBytes(buff, bytesToRead);
      if (bytesRead > 0) {
        lastDataAt = millis();
        Update.write(buff, bytesRead);
        written += bytesRead;
        int percent = (written * 100) / contentLength;
        if (percent != lastPercent && (millis() - lastProgressTime > 150 || percent == 100)) {
          lastPercent = percent;
          lastProgressTime = millis();
          view_.showOtaProgress(percent, "ກຳລັງດາວໂຫຼດ...");
          washerui::App.update();
          if (percent % 10 == 0 && mqtt_.connected()) {
            publish(tidTopic("/ota/status"), "progress:" + String(percent));
            mqtt_.loop();
          }
          Serial.printf("[OTA] %d%% (%d / %d bytes)\n", percent, written, contentLength);
        }
      }
    } else {
      if (millis() - lastDataAt > 15000) {
        Serial.println("[OTA] Stream timeout: no data for 15s");
        break;
      }
      delay(1);
    }
  }

  if (written == contentLength && Update.end(true)) {
    if (Update.isFinished()) {
      Serial.println("[OTA] Update completed successfully! Rebooting...");
      view_.showOtaProgress(100, "ອັບເດດສຳເລັດ! ກຳລັງຣີສະຕາດ...");
      washerui::App.update();

      const String clientId = "WASHER_OTA_" + settings_.terminalId;
      if (settings_.mqttUser.isEmpty() ? mqtt_.connect(clientId.c_str()) : mqtt_.connect(clientId.c_str(), settings_.mqttUser.c_str(), settings_.mqttPassword.c_str())) {
        mqtt_.publish(tidTopic("/ota/status").c_str(), "success");
        mqtt_.loop();
        delay(200);
      }

      for (int i = 0; i < 20; ++i) {
        esp_task_wdt_reset();
        washerui::App.update();
        delay(100);
      }
      ESP.restart();
    } else {
      Serial.println("[OTA] Update not finished!");
      Update.abort();
      publish(tidTopic("/ota/status"), "failed: update not finished");
      view_.showOtaProgress(0, "Not finished");
      for (int i = 0; i < 30; ++i) {
        esp_task_wdt_reset();
        washerui::App.update();
        delay(100);
      }
      view_.hideOtaProgress();
    }
  } else {
    Serial.printf("[OTA] Update failed or timed out, code: %u\n", Update.getError());
    Update.abort();
    publish(tidTopic("/ota/status"), "failed: download error or timeout");
    view_.showOtaProgress(0, "Update failed!");
    for (int i = 0; i < 30; ++i) {
      esp_task_wdt_reset();
      washerui::App.update();
      delay(100);
    }
    view_.hideOtaProgress();
  }

  http.end();
  isUpdatingOta_ = false;
}
