#pragma once

#include <Arduino.h>
#include <WasherUI.h>

#include "AppSettings.h"
#include "Assets.h"

enum class Page : uint8_t { Home, Select, QrCode, Payment, PaymentSuccess, Running, Success, Pause, Pin, Settings, WifiScan, SelectBank };

class WasherViewListener {
 public:
  virtual void onOptionSelected(uint8_t option) = 0;
  virtual void onBankSelected(const char* bankCode) = 0;
  virtual void onPricePageOpened() = 0;
  virtual void onBackRequested() = 0;
  virtual void onSettingsSaved() = 0;
  virtual void onRelayTest(uint8_t option, uint8_t pulseCount) = 0;
  virtual void onWifiConnectRequested(uint8_t profile, const String& ssid, const String& password) = 0;
  virtual void onWifiScanStarted() = 0;
  virtual void onWifiScanEnded() = 0;
  virtual void beep(uint8_t count) = 0;
};

class WasherView {
 public:
  void begin(AppSettings& settings, WasherViewListener& listener);
  void playBootSplash();
  void update();
  void show(Page page);
  void setPrice(uint8_t option, uint32_t price);
  void setSelectBankOption(uint8_t option);
  void setSelectedBank(const char* bankCode);
  void setQrCode(const String& payload);
  void setQrOption(uint8_t option);
  void setQrTimeoutSeconds(uint32_t seconds);
  void setCountdown(uint32_t seconds, uint8_t option);
  void setConnection(bool wifiConnected, bool mqttConnected, int rssi = -100);
  void setWifiStatus(const String& status);
  void setActiveWifi(uint8_t profile, bool connected, const String& ssid, const String& ip);
  void setWifiProfiles(const String& ssid1, const String& password1,
                       const String& ssid2, const String& password2);
  void setPauseText(const char* text);
  void showResetPopup();
  void hideResetPopup();
  void showOtaProgress(uint8_t percent, const char* message);
  void hideOtaProgress();
  Page page() const { return currentPage_; }
  bool isQrLoaded() const { return qrCodeLoaded_; }

 private:
  static WasherView* instance_;
  static void optionClicked(washerui::Object&, void* context);
  static void priceCardClicked(lv_event_t* event);
  static void bankCardClicked(lv_event_t* event);
  static void backClicked(washerui::Object&, void* context);
  static void settingsClicked(washerui::Object&, void* context);
  static void saveClicked(washerui::Object&, void* context);
  static void secretEvent(lv_event_t* event);
  static void pinKeyEvent(lv_event_t* event);
  static void textAreaEvent(lv_event_t* event);
  static void keyboardEvent(lv_event_t* event);
  static void brightnessEvent(lv_event_t* event);
  static void brightnessStepEvent(lv_event_t* event);
  static void volumeEvent(lv_event_t* event);
  static void volumeStepEvent(lv_event_t* event);
  static void wifiConnectEvent(lv_event_t* event);
  static void wifiScanEvent(lv_event_t* event);
  static void wifiPassToggleEvent(lv_event_t* event);
  static void wifiSwitchEvent(lv_event_t* event);
  static void wifiListItemClicked(lv_event_t* event);
  static void wifiRescanClicked(washerui::Object&, void* context);
  static void wifiScanBackClicked(washerui::Object&, void* context);
  static void optionTabSelectEvent(lv_event_t* event);
  static void optionPulseAdjustEvent(lv_event_t* event);
  static void optionTestRelayEvent(lv_event_t* event);
  void updateOptionTabDisplay();

  void buildPages();
  void buildSelectBankPage();
  void buildPinPage();
  void buildSettings();
  void buildWifiScanPage();
  void startWifiScan(uint8_t targetProfile);
  void configurePage(washerui::Rect& page, uint32_t color);
  void saveSettings();
  void appendPin(char key);
  void openSettingsGate();
  void showKeyboard(lv_obj_t* textArea, bool numeric = false);
  void animateStatusPages(uint32_t now);
  void revealQrCode();
  String numberFormat(uint32_t value) const;
  lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, int x, int y, const lv_font_t* font = nullptr);
  lv_obj_t* makeTextArea(lv_obj_t* parent, int x, int y, int width, const char* text, bool password = false);

  AppSettings* settings_ = nullptr;
  WasherViewListener* listener_ = nullptr;
  Page currentPage_ = Page::Home;
  uint32_t imageSwapAt_ = 0;
  uint32_t secretPressedAt_ = 0;
  uint32_t settingsButtonHideAt_ = 0;
  uint32_t nextAnimationAt_ = 0;
  uint32_t pageEnteredAt_ = 0;
  bool alternateHomeImage_ = false;
  bool wifiConnected_ = false;
  bool mqttConnected_ = false;
  int rssi_ = -100;
  String enteredPin_;
  uint32_t prices_[3] = {};

  washerui::Rect home_;
  washerui::Image homeImage_{&img_1};
  washerui::Button settingsButton_{LV_SYMBOL_SETTINGS};
  lv_obj_t* homeClockPill_ = nullptr;
  lv_obj_t* homeClockLabel_ = nullptr;
  uint32_t nextClockUpdateAt_ = 0;
  lv_obj_t* wifiCont_ = nullptr;
  lv_obj_t* wifiDot_ = nullptr;
  lv_obj_t* wifiArcs_[3] = {nullptr};
  lv_obj_t* netGlobe_ = nullptr;
  lv_obj_t* netGlobeVLine_ = nullptr;
  lv_obj_t* netGlobeHLine_ = nullptr;
  lv_obj_t* secretArea_ = nullptr;
  lv_obj_t* popupBackdrop_ = nullptr;
  lv_obj_t* resetPopup_ = nullptr;
  lv_obj_t* otaPopup_ = nullptr;
  lv_obj_t* otaBar_ = nullptr;
  lv_obj_t* otaPercentLabel_ = nullptr;
  lv_obj_t* otaMsgLabel_ = nullptr;

  washerui::Rect select_;
  washerui::Label selectTitle_{"ເລືອກຈ່າຍເງິນຕາມອຸນຫະພູມທີ່ເລືອກ"};
  washerui::Label selectSubTitle_{"Choose payment by temperature"};
  lv_obj_t* selectDivider_ = nullptr;
  washerui::Button optionButtons_[3];
  washerui::Label priceLabels_[3];
  lv_obj_t* priceCards_[3] = {};
  washerui::Button selectBack_{LV_SYMBOL_LEFT};

  washerui::Rect selectBank_;
  washerui::Label selectBankTitle_{"ເລືອກທະນາຄານ ຫຼື ກະເປົາເງິນ"};
  washerui::Label selectBankSubTitle_{"Select Bank or E-Wallet"};
  lv_obj_t* selectBankDivider_ = nullptr;
  washerui::Button selectBankBack_{LV_SYMBOL_LEFT};
  lv_obj_t* bankCards_[6] = {};
  lv_obj_t* selectBankBottomBar_ = nullptr;
  washerui::Label selectBankInfoLabel_;
  void updateSelectBankInfo(uint8_t option);

  washerui::Rect qr_;
  washerui::Label qrTitle_{"ສະແກນຈ່າຍເງິນດ້ວຍແອບທະນາຄານຫຼືກະເປົ໋າເງິນໂທລະສັບ"};
  washerui::Label qrSubTitle_{"Scan to pay with Mobile Banking or E-Wallet"};
  lv_obj_t* qrDivider_ = nullptr;
  washerui::Image laoQr_{&img_laoQR};
  washerui::Label qrPrice_;
  washerui::Button qrBack_{LV_SYMBOL_LEFT};
  lv_obj_t* qrCard_ = nullptr;
  lv_obj_t* qrCode_ = nullptr;
  lv_obj_t* qrBottomBar_ = nullptr;
  washerui::Label qrInfoLabel_;
  lv_obj_t* qrLoadingBox_ = nullptr;
  lv_obj_t* qrLoadingHalo_ = nullptr;
  lv_obj_t* qrLoadingArc_ = nullptr;
  lv_obj_t* qrLoadingInnerArc_ = nullptr;
  lv_obj_t* qrScannerBox_ = nullptr;
  lv_obj_t* qrFinderBlocks_[4] = {};
  lv_obj_t* qrScanLaser_ = nullptr;
  lv_obj_t* qrLoadingBubbles_[4] = {};
  washerui::Label qrLoadingLabel_{"ກຳລັງໂຫຼດ"};
  lv_obj_t* qrTimeoutCard_ = nullptr;
  lv_obj_t* qrLapnetCard_ = nullptr;
  lv_obj_t* qrLapnetImg_ = nullptr;
  lv_obj_t* qrLapnetTitle_ = nullptr;
  lv_obj_t* qrLapnetSub_ = nullptr;
  lv_obj_t* qrTimeoutArc_ = nullptr;
  lv_obj_t* qrTimeoutValLabel_ = nullptr;
  uint32_t qrTimeoutSec_ = 30;
  bool qrCodeLoaded_ = false;
  bool qrCodeReceived_ = false;
  String qrPendingPayload_ = "";
  uint32_t qrAnimationStartedAt_ = 0;
  uint8_t qrSelectedOption_ = 1;
  void updateQrInfo(uint8_t option);

  washerui::Rect payment_;
  washerui::Label paymentText_{"ກຳລັງເຕີມເຄຣດິດເຂົ້າເຄື່ອງ\nກະລຸນາລໍຖ້າ...."};
  lv_obj_t* paymentCard_ = nullptr;
  lv_obj_t* paymentMachine_ = nullptr;
  lv_obj_t* paymentDrum_ = nullptr;
  lv_obj_t* paymentHalo_ = nullptr;
  lv_obj_t* creditDots_[3] = {};
  washerui::Rect paymentSuccess_;
  washerui::Label paymentSuccessText_{"ການຊຳລະເງິນສຳເລັດ"};
  lv_obj_t* paymentSuccessCard_ = nullptr;
  lv_obj_t* paymentSuccessHalo_ = nullptr;
  lv_obj_t* paymentSuccessArc_ = nullptr;
  lv_obj_t* paymentSuccessCircle_ = nullptr;
  lv_obj_t* paymentSuccessCheck_ = nullptr;
  lv_obj_t* paymentSuccessSparks_[8] = {};
  washerui::Rect running_;
  washerui::Label runningText_{"ເຄື່ອງຊັກກຳລັງເຮັດວຽກ\nກະລຸນາລໍຖ້າ...."};
  washerui::Label countdownText_;
  lv_obj_t* runningMachine_ = nullptr;
  lv_obj_t* runningDrum_ = nullptr;
  lv_obj_t* runningInnerRing_ = nullptr;
  lv_obj_t* runningWaterWave1_ = nullptr;
  lv_obj_t* runningWaterWave2_ = nullptr;
  lv_obj_t* runningPaddles_[3] = {};
  lv_obj_t* runningBubbles_[5] = {};
  lv_obj_t* runningStatusLed_ = nullptr;
  lv_obj_t* runningTempPill_ = nullptr;
  lv_obj_t* runningTempLabel_ = nullptr;
  lv_obj_t* runningProgressBar_ = nullptr;
  uint8_t runningActiveOption_ = 1;
  washerui::Rect success_;
  washerui::Label successIcon_{LV_SYMBOL_OK};
  washerui::Label successText_{"ຊັກສຳເລັດແລ້ວ"};
  lv_obj_t* successMachine_ = nullptr;
  lv_obj_t* successDrum_ = nullptr;
  lv_obj_t* successHalo_ = nullptr;
  lv_obj_t* successSparks_[4] = {};
  washerui::Rect pause_;
  washerui::Label pauseIcon_{LV_SYMBOL_WARNING};
  washerui::Label pauseText_{"ຢຸດໃຫ້ບໍລິການຊົ່ວຄາວ"};
  lv_obj_t* pauseMachine_ = nullptr;
  lv_obj_t* pauseDrum_ = nullptr;
  lv_obj_t* pauseInnerRing_ = nullptr;
  lv_obj_t* pauseHalo_ = nullptr;
  lv_obj_t* pauseCircle_ = nullptr;
  lv_obj_t* pauseStatusLed_ = nullptr;
  lv_obj_t* pauseRadarDots_[4] = {};
  lv_obj_t* pauseConsole_ = nullptr;

  washerui::Rect pinPage_;
  washerui::Button pinBack_{LV_SYMBOL_LEFT};
  washerui::Label pinTitle_{"ໃສ່ລະຫັດ PIN"};
  washerui::Label pinDots_;
  washerui::Label pinError_;
  lv_obj_t* pinKeys_[12] = {};

  washerui::Rect settingsPage_;
  washerui::Button settingsBack_{LV_SYMBOL_LEFT};
  washerui::Button saveButton_{LV_SYMBOL_SAVE};
  lv_obj_t* tabView_ = nullptr;
  lv_obj_t* terminalInput_ = nullptr;
  lv_obj_t* mqttHostInput_ = nullptr;
  lv_obj_t* mqttPortInput_ = nullptr;
  lv_obj_t* mqttUserInput_ = nullptr;
  lv_obj_t* mqttPassInput_ = nullptr;
  lv_obj_t* mqttPassToggle_ = nullptr;
  lv_obj_t* pinInput_ = nullptr;
  lv_obj_t* wifiSsidInput_ = nullptr;
  lv_obj_t* wifiPasswordInput_ = nullptr;
  lv_obj_t* wifiSsid2Input_ = nullptr;
  lv_obj_t* wifiPassword2Input_ = nullptr;
  lv_obj_t* wifiScanBtn_[2] = {};
  lv_obj_t* wifiPassToggle_[2] = {};
  lv_obj_t* wifiSwitch_[2] = {};
  lv_obj_t* wifiBadge_[2] = {};
  lv_obj_t* wifiStatusLabel_ = nullptr;
  lv_obj_t* brightnessSlider_ = nullptr;
  lv_obj_t* brightnessValue_ = nullptr;
  lv_obj_t* volumeSlider_ = nullptr;
  lv_obj_t* volumeValue_ = nullptr;
  uint8_t selectedOptionTab_ = 0;
  lv_obj_t* optionTabBtns_[3] = {};
  lv_obj_t* optionPulseLabel_ = nullptr;
  lv_obj_t* timeDropdown_[3] = {};
  lv_obj_t* keyboard_ = nullptr;
  lv_obj_t* keyboardPreviewCont_ = nullptr;
  lv_obj_t* keyboardPreviewTitle_ = nullptr;
  lv_obj_t* keyboardPreviewInput_ = nullptr;
  lv_obj_t* activeTargetTextArea_ = nullptr;
  lv_obj_t* saveBackdrop_ = nullptr;
  lv_obj_t* saveModal_ = nullptr;
  lv_obj_t* saveModalBar_ = nullptr;
  uint32_t saveModalHideAt_ = 0;

  washerui::Rect wifiScanPage_;
  washerui::Button wifiScanBack_{LV_SYMBOL_LEFT};
  washerui::Button wifiRescanBtn_{"ສະແກນໃໝ່"};
  washerui::Label wifiScanTitle_{"ຄົ້ນຫາເຄືອຂ່າຍ Wi-Fi"};
  washerui::Label wifiScanStatusLabel_;
  lv_obj_t* wifiScanSpinner_ = nullptr;
  lv_obj_t* wifiScanList_ = nullptr;
  uint8_t scanTargetProfile_ = 0;
  bool scanningActive_ = false;
  uint32_t scanStartedAt_ = 0;
  String scannedSsids_[32];
  uint16_t scannedCount_ = 0;
};
