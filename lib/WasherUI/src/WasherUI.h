#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <driver/spi_master.h>

#include "LaoText.h"

namespace washerui {

enum class Align {
  Center,
  TopLeft,
  TopCenter,
  TopRight,
  BottomLeft,
  BottomCenter,
  BottomRight,
  LeftCenter,
  RightCenter,
};

class Object {
 public:
  using EventCallback = void (*)(Object& sender, void* context);

  Object() = default;
  Object(const Object&) = delete;
  Object& operator=(const Object&) = delete;
  virtual ~Object() = default;

  Object& parent(Object& parent);
  Object& size(int32_t width, int32_t height);
  Object& position(int32_t x, int32_t y);
  Object& align(Align alignment, int32_t x_offset = 0, int32_t y_offset = 0);
  Object& color(uint32_t rgb888);
  Object& opacity(uint8_t opacity);
  Object& radius(int32_t radius);
  Object& border(int32_t width, uint32_t rgb888);
  Object& padding(int32_t top, int32_t right, int32_t bottom, int32_t left);
  Object& hidden(bool hidden = true);
  Object& clickable(bool enabled = true);
  Object& onClicked(EventCallback callback, void* context = nullptr);
  Object& font(const lv_font_t* font);

  bool isCreated() const { return object_ != nullptr; }
  lv_obj_t* native();

 protected:
  virtual void create() = 0;
  void ensureCreated();
  lv_obj_t* parentNative();
  void setNative(lv_obj_t* object) { object_ = object; }

 private:
  static void eventThunk(lv_event_t* event);
  static lv_align_t toLvAlign(Align alignment);

  lv_obj_t* object_ = nullptr;
  Object* parent_ = nullptr;
  EventCallback clicked_callback_ = nullptr;
  void* clicked_context_ = nullptr;
};

class Rect : public Object {
 protected:
  void create() override;
};

class Label : public Object {
 public:
  Label() = default;
  explicit Label(const char* text) : pending_text_(text) {}

  Label& text(const char* text);
  Label& textColor(uint32_t rgb888);
  Label& operator=(const char* text) { return this->text(text); }
  Label& operator=(int32_t value);

 protected:
  void create() override;

 private:
  String pending_text_;
};

class Button : public Object {
 public:
  Button() = default;
  explicit Button(const char* text) : pending_text_(text) {}

  Button& text(const char* text);
  Label& label();

 protected:
  void create() override;

 private:
  String pending_text_;
  Label label_;
};

class Image : public Object {
 public:
  Image() = default;
  explicit Image(const void* source) : source_(source) {}

  Image& source(const void* source);

 protected:
  void create() override;

 private:
  const void* source_ = nullptr;
};

// Native QSPI Display Driver for LCDWIKI ES3C35P (ST77922 320x480 TDDI Panel)
class ES3C35PDisplay {
 public:
  // Logical Landscape dimensions used by LVGL UI
  static constexpr int32_t ScreenWidth = 480;
  static constexpr int32_t ScreenHeight = 320;
  // Physical Portrait dimensions of ST77922 panel
  static constexpr int32_t NativeWidth = 320;
  static constexpr int32_t NativeHeight = 480;

  bool init();
  void setBrightness(uint8_t brightness);
  void fillScreen(uint16_t color565);
  void drawBitmap(int x_start, int y_start, int x_end, int y_end, const void* color_data);

 private:
  void txParam(int lcd_cmd, const void* param, size_t param_size);
  void txColor(const uint16_t* data, size_t pixel_count);

  spi_device_handle_t spi_dev_ = nullptr;
};

// Capacitive Incell Touch Driver for ST77922 (I2C Address 0x55 on SDA=38, SCL=39)
class ST77922Touch {
 public:
  bool begin(TwoWire& wire);
  bool read(uint16_t& x, uint16_t& y);
  bool available() const { return initialized_; }

 private:
  bool readRegister(uint16_t reg, uint8_t* data, size_t length);

  TwoWire* wire_ = nullptr;
  bool initialized_ = false;
  uint8_t max_points_ = 1;
  bool last_pressed_ = false;
  uint16_t last_x_ = 0;
  uint16_t last_y_ = 0;
  uint32_t last_sample_at_ = 0;
};

// Onboard ES8311 I2S Audio Codec + FM8002E PA Speaker Driver for ES3C35P
class ES8311Audio {
 public:
  bool begin(TwoWire& wire, uint8_t initial_volume = 100);
  void setVolume(uint8_t volume);
  uint8_t volume() const { return volume_; }
  void playTone(uint16_t freq_hz, uint32_t duration_ms);
  bool available() const { return initialized_; }

 private:
  bool writeReg(uint8_t reg, uint8_t val);
  bool readReg(uint8_t reg, uint8_t& val);

  TwoWire* wire_ = nullptr;
  uint8_t addr_ = 0x18;
  uint8_t volume_ = 100;
  bool initialized_ = false;
};

class Application {
 public:
  bool begin(uint8_t brightness = 255, uint8_t volume = 100);
  void update();
  void brightness(uint8_t value);
  void volume(uint8_t value);
  uint8_t volume() const { return audio_.volume(); }
  void playSpeakerBeep(uint16_t freq_hz = 2400, uint32_t duration_ms = 100);
  ES3C35PDisplay& display() { return display_; }
  bool touchAvailable() const { return touch_.available(); }
  bool speakerAvailable() const { return audio_.available(); }
  bool touching() const { return touching_; }
  uint16_t touchX() const { return touch_x_; }
  uint16_t touchY() const { return touch_y_; }
  uint32_t touchSequence() const { return touch_sequence_; }
  uint32_t lastTouchAt() const { return last_touch_at_; }

 private:
  static void flush(lv_disp_drv_t* display, const lv_area_t* area, lv_color_t* pixels);
  static void rounder(lv_disp_drv_t* display, lv_area_t* area);
  static void readTouch(lv_indev_drv_t* input, lv_indev_data_t* data);
  bool allocateDrawBuffers();

  static Application* instance_;
  ES3C35PDisplay display_;
  ST77922Touch touch_;
  ES8311Audio audio_;
  lv_disp_draw_buf_t draw_buffer_descriptor_{};
  lv_disp_drv_t display_driver_{};
  lv_indev_drv_t touch_driver_{};
  bool touching_ = false;
  uint16_t touch_x_ = 0;
  uint16_t touch_y_ = 0;
  uint32_t touch_sequence_ = 0;
  uint32_t last_touch_at_ = 0;
  lv_color_t* draw_buffer_[2] = {nullptr, nullptr};
  uint16_t* rot_buffer_ = nullptr;
  uint32_t draw_buffer_pixels_ = 0;
};

extern Application App;

}  // namespace washerui
