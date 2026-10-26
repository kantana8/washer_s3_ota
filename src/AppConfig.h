#pragma once

#include <Arduino.h>

namespace config {

constexpr char FirmwareVersion[] = "v1.9";

constexpr char MqttHost[] = "broker.hivemq.com";
constexpr uint16_t MqttPort = 1883;
constexpr char MqttUser[] = "";
constexpr char MqttPassword[] = "";

// LCDWIKI ES3C35P (ESP32-S3) Expansion 4-pin header exposes IO45 and IO46
constexpr uint8_t RelayPin = 45;
constexpr uint8_t BuzzerPin = 46;

constexpr uint32_t PageTimeoutMs = 20000;
constexpr uint32_t QrScanTimeoutMs = 30000;
constexpr uint32_t CreditTransferMs = 2200;
constexpr uint32_t PaymentSuccessMs = 2200;
constexpr uint32_t SuccessDelayMs = 5000;

}  // namespace config
