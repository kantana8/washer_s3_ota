#include "WasherUI.h"
#include "LaoText.h"

#include <driver/i2s.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_commands.h>
#include <hal/gpio_ll.h>
#include <math.h>

namespace washerui {

namespace {

// ---- LCDWIKI ES3C35P Hardware Pin Definitions ----
constexpr int PinLcdCs        = 10;
constexpr int PinLcdClk       = 12;
constexpr int PinLcdD0        = 11;
constexpr int PinLcdD1        = 13;
constexpr int PinLcdD2        = 14;
constexpr int PinLcdD3        = 9;
constexpr int PinLcdBacklight = 41;
constexpr int BacklightPwmCh  = 7;

constexpr int PinI2cSda       = 38;
constexpr int PinI2cScl       = 39;
constexpr int PinTouchRst     = 48;
constexpr int PinTouchInt     = 47;
constexpr uint8_t TouchI2cAddr = 0x55;

constexpr int PinAudioPa      = 1;   // FM8002E Shutdown (Active LOW: LOW = amplifier ON, HIGH = mute)
constexpr int PinI2sMclk      = 17;
constexpr int PinI2sBclk      = 18;
constexpr int PinI2sWs        = 21;
constexpr int PinI2sDout      = 15;
constexpr int PinI2sDin       = 16;

constexpr uint32_t QspiOpcodeWriteCmd   = 0x02ULL;
constexpr uint32_t QspiOpcodeWriteColor = 0x32ULL;

struct St77922Cmd {
  uint8_t cmd;
  const uint8_t* data;
  size_t len;
  uint16_t delay_ms;
};

// Official LCDWIKI ES3C35P ST77922 initialization sequence (from Install libraries/ST77922/ST77922.cpp)
const St77922Cmd kSt77922InitCmds[] = {
    {0xF1, (const uint8_t[]){0x00}, 1, 0},
    {0x60, (const uint8_t[]){0x00, 0x00, 0x00}, 3, 0},
    {0x65, (const uint8_t[]){0x80}, 1, 0},
    {0x79, (const uint8_t[]){0x06}, 1, 0},
    {0x7B, (const uint8_t[]){0x00, 0x08, 0x08}, 3, 0},
    {0x80, (const uint8_t[]){0x55, 0x62, 0x2F, 0x17, 0xF0, 0x52, 0x70, 0xD2, 0x52, 0x62, 0xEA}, 11, 0},
    {0x81, (const uint8_t[]){0x26, 0x52, 0x72, 0x27}, 4, 0},
    {0x84, (const uint8_t[]){0x92, 0x25}, 2, 0},
    {0x87, (const uint8_t[]){0x10, 0x10, 0x58, 0x00, 0x02, 0x3A}, 6, 0},
    {0x88, (const uint8_t[]){0x00, 0x00, 0x2C, 0x10, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06}, 15, 0},
    {0x89, (const uint8_t[]){0x00, 0x00, 0x00}, 3, 0},
    {0x8A, (const uint8_t[]){0x13, 0x00, 0x2C, 0x00, 0x00, 0x2C, 0x10, 0x10, 0x00, 0x3E, 0x19}, 11, 0},
    {0x8B, (const uint8_t[]){0x15, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x97, 0x8E}, 9, 0},
    {0x8C, (const uint8_t[]){0x1D, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x50, 0x0F, 0x01, 0xC5, 0x12, 0x09}, 13, 0},
    {0x8D, (const uint8_t[]){0x0C}, 1, 0},
    {0x8E, (const uint8_t[]){0x33, 0x01, 0x0C, 0x13, 0x01, 0x01}, 6, 0},
    {0xB3, (const uint8_t[]){0x00, 0x30}, 2, 0},
    {0xF1, (const uint8_t[]){0x00}, 1, 0},
    {0x71, (const uint8_t[]){0xD0}, 1, 0},
    {0x66, (const uint8_t[]){0x02, 0x3F}, 2, 0},
    {0xBE, (const uint8_t[]){0x26, 0x00, 0x9D}, 3, 0},
    {0x70, (const uint8_t[]){0x01, 0xA0, 0x11, 0x40, 0xE0, 0x00, 0x11, 0x69, 0x11, 0x00, 0x00, 0x1A}, 12, 0},
    {0x90, (const uint8_t[]){0x04, 0x04, 0x55, 0x74, 0x00, 0x40, 0x43, 0x27, 0x27}, 9, 0},
    {0x91, (const uint8_t[]){0x04, 0x04, 0x55, 0x75, 0x00, 0x40, 0x42, 0x27, 0x27}, 9, 0},
    {0x92, (const uint8_t[]){0x04, 0x44, 0x55, 0xC0, 0x06, 0x00, 0x07, 0x05, 0x90, 0x27}, 10, 0},
    {0x93, (const uint8_t[]){0x04, 0x43, 0x11, 0x00, 0x00, 0x00, 0x00, 0x05, 0x90, 0x27}, 10, 0},
    {0x94, (const uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 6, 0},
    {0x95, (const uint8_t[]){0x96, 0x16, 0x00, 0x00, 0xFF}, 5, 0},
    {0x96, (const uint8_t[]){0x44, 0x53, 0x03, 0x12, 0x23, 0x24, 0x06, 0x05, 0x94, 0x27, 0x00, 0x44}, 12, 0},
    {0x97, (const uint8_t[]){0x44, 0x53, 0x47, 0x56, 0x20, 0x20, 0x02, 0x01, 0x94, 0x27, 0x00, 0x44}, 12, 0},
    {0xBA, (const uint8_t[]){0x55, 0x94, 0x2D, 0x94, 0x27}, 5, 0},
    {0x9A, (const uint8_t[]){0x40, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9B, (const uint8_t[]){0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9C, (const uint8_t[]){0x5C, 0x12, 0x00, 0x00, 0x10, 0x12, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00}, 13, 0},
    {0x9D, (const uint8_t[]){0x8A, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 8, 0},
    {0x9E, (const uint8_t[]){0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 7, 0},
    {0xB4, (const uint8_t[]){0x1D, 0x1C, 0x1E, 0x0B, 0x14, 0x02, 0x13, 0x09, 0x1E, 0x00, 0x1E, 0x10}, 12, 0},
    {0xB5, (const uint8_t[]){0x1D, 0x1C, 0x1E, 0x0A, 0x15, 0x03, 0x11, 0x08, 0x1E, 0x01, 0x1E, 0x12}, 12, 0},
    {0xB6, (const uint8_t[]){0x77, 0x77, 0x00, 0x0A, 0xFF, 0x0A, 0xFF}, 7, 0},
    {0x86, (const uint8_t[]){0xCD, 0x04, 0xB1, 0x02, 0x58, 0x12, 0x58, 0x0C, 0x13, 0x01, 0xA5, 0x00, 0xA5, 0xA5}, 14, 0},
    {0xB7, (const uint8_t[]){0x07, 0x0A, 0x0E, 0x06, 0x05, 0x03, 0x2B, 0x03, 0x03, 0x42, 0x07, 0x10, 0x10, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB8, (const uint8_t[]){0x07, 0x0A, 0x0D, 0x05, 0x05, 0x02, 0x2B, 0x02, 0x03, 0x42, 0x06, 0x10, 0x0F, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB9, (const uint8_t[]){0x23, 0x23}, 2, 0},
    {0xBF, (const uint8_t[]){0x10, 0x14, 0x14, 0x0B, 0x0B, 0x0B}, 6, 0},
    {0xF2, (const uint8_t[]){0x00}, 1, 0},
    {0x73, (const uint8_t[]){0x04, 0xDA, 0x12, 0x54, 0x47}, 5, 0},
    {0x77, (const uint8_t[]){0x6B, 0x5B, 0xFD, 0xC3, 0xC5}, 5, 0},
    {0x7A, (const uint8_t[]){0x15, 0x27}, 2, 0},
    {0x7B, (const uint8_t[]){0x04, 0x57}, 2, 0},
    {0x7E, (const uint8_t[]){0x01, 0x0E}, 2, 0},
    {0xBF, (const uint8_t[]){0x36}, 1, 0},
    {0xE3, (const uint8_t[]){0x40, 0x40}, 2, 0},
    {0xF0, (const uint8_t[]){0x00}, 1, 0},
    {0xD0, (const uint8_t[]){0x00}, 1, 0},
    {0x2A, (const uint8_t[]){0x00, 0x00, 0x01, 0x3F}, 4, 0},
    {0x2B, (const uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x21, nullptr, 0, 0},
    {0x11, nullptr, 0, 120},
    {0x29, nullptr, 0, 0},
    {0x2C, nullptr, 0, 0},
    {0x3A, (const uint8_t[]){0x01}, 1, 0},
    {0x36, (const uint8_t[]){0x00}, 1, 0},
    {0x35, (const uint8_t[]){0x01}, 1, 20},
};

}  // namespace

Application App;
Application* Application::instance_ = nullptr;

void Object::ensureCreated() {
  if (!object_) create();
}

lv_obj_t* Object::parentNative() {
  if (!parent_) return lv_scr_act();
  return parent_->native();
}

lv_obj_t* Object::native() {
  ensureCreated();
  return object_;
}

Object& Object::parent(Object& parent_object) {
  parent_ = &parent_object;
  if (object_) lv_obj_set_parent(object_, parent_object.native());
  return *this;
}

Object& Object::size(int32_t width, int32_t height) {
  ensureCreated();
  lv_obj_set_size(object_, width, height);
  return *this;
}

Object& Object::position(int32_t x, int32_t y) {
  ensureCreated();
  lv_obj_set_pos(object_, x, y);
  return *this;
}

Object& Object::align(Align alignment, int32_t x_offset, int32_t y_offset) {
  ensureCreated();
  lv_obj_align(object_, toLvAlign(alignment), x_offset, y_offset);
  return *this;
}

Object& Object::color(uint32_t rgb888) {
  ensureCreated();
  lv_obj_set_style_bg_color(object_, lv_color_hex(rgb888), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object_, LV_OPA_COVER, LV_PART_MAIN);
  return *this;
}

Object& Object::opacity(uint8_t opacity_value) {
  ensureCreated();
  lv_obj_set_style_opa(object_, opacity_value, LV_PART_MAIN);
  return *this;
}

Object& Object::radius(int32_t radius_value) {
  ensureCreated();
  lv_obj_set_style_radius(object_, radius_value, LV_PART_MAIN);
  return *this;
}

Object& Object::border(int32_t width, uint32_t rgb888) {
  ensureCreated();
  lv_obj_set_style_border_width(object_, width, LV_PART_MAIN);
  lv_obj_set_style_border_color(object_, lv_color_hex(rgb888), LV_PART_MAIN);
  return *this;
}

Object& Object::padding(int32_t top, int32_t right, int32_t bottom, int32_t left) {
  ensureCreated();
  lv_obj_set_style_pad_top(object_, top, LV_PART_MAIN);
  lv_obj_set_style_pad_right(object_, right, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(object_, bottom, LV_PART_MAIN);
  lv_obj_set_style_pad_left(object_, left, LV_PART_MAIN);
  return *this;
}

Object& Object::hidden(bool should_hide) {
  ensureCreated();
  if (should_hide) {
    lv_obj_add_flag(object_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(object_, LV_OBJ_FLAG_HIDDEN);
  }
  return *this;
}

Object& Object::clickable(bool enabled) {
  ensureCreated();
  if (enabled) {
    lv_obj_add_flag(object_, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_clear_flag(object_, LV_OBJ_FLAG_CLICKABLE);
  }
  return *this;
}

Object& Object::onClicked(EventCallback callback, void* context) {
  ensureCreated();
  clicked_callback_ = callback;
  clicked_context_ = context;
  lv_obj_add_event_cb(object_, eventThunk, LV_EVENT_CLICKED, this);
  return *this;
}

Object& Object::font(const lv_font_t* font_value) {
  ensureCreated();
  lv_obj_set_style_text_font(object_, font_value, LV_PART_MAIN);
  return *this;
}

void Object::eventThunk(lv_event_t* event) {
  auto* self = static_cast<Object*>(lv_event_get_user_data(event));
  if (self && self->clicked_callback_) {
    self->clicked_callback_(*self, self->clicked_context_);
  }
}

lv_align_t Object::toLvAlign(Align alignment) {
  switch (alignment) {
    case Align::TopLeft: return LV_ALIGN_TOP_LEFT;
    case Align::TopCenter: return LV_ALIGN_TOP_MID;
    case Align::TopRight: return LV_ALIGN_TOP_RIGHT;
    case Align::BottomLeft: return LV_ALIGN_BOTTOM_LEFT;
    case Align::BottomCenter: return LV_ALIGN_BOTTOM_MID;
    case Align::BottomRight: return LV_ALIGN_BOTTOM_RIGHT;
    case Align::LeftCenter: return LV_ALIGN_LEFT_MID;
    case Align::RightCenter: return LV_ALIGN_RIGHT_MID;
    case Align::Center:
    default: return LV_ALIGN_CENTER;
  }
}

void Rect::create() {
  if (isCreated()) return;
  setNative(lv_obj_create(parentNative()));
  lv_obj_clear_flag(native(), LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(native(), LV_OBJ_FLAG_CLICKABLE);
}

void Label::create() {
  if (isCreated()) return;
  setNative(lv_label_create(parentNative()));
  pending_text_ = shapeLaoText(pending_text_.c_str());
  lv_label_set_text(native(), pending_text_.c_str());
}

Label& Label::text(const char* value) {
  pending_text_ = shapeLaoText(value ? value : "");
  ensureCreated();
  lv_label_set_text(native(), pending_text_.c_str());
  return *this;
}

Label& Label::textColor(uint32_t rgb888) {
  ensureCreated();
  lv_obj_set_style_text_color(native(), lv_color_hex(rgb888), LV_PART_MAIN);
  return *this;
}

Label& Label::operator=(int32_t value) {
  pending_text_ = String(value);
  return text(pending_text_.c_str());
}

void Button::create() {
  if (isCreated()) return;
  setNative(lv_btn_create(parentNative()));
  label_.parent(*this);
  label_.text(pending_text_.c_str());
  label_.align(Align::Center);
}

void Image::create() {
  if (isCreated()) return;
  setNative(lv_img_create(parentNative()));
  if (source_) lv_img_set_src(native(), source_);
}

Image& Image::source(const void* source_value) {
  source_ = source_value;
  ensureCreated();
  lv_img_set_src(native(), source_);
  return *this;
}

Button& Button::text(const char* value) {
  pending_text_ = value ? value : "";
  ensureCreated();
  label_.text(pending_text_.c_str());
  return *this;
}

Label& Button::label() {
  ensureCreated();
  return label_;
}

// ============================================================================
// ES3C35PDisplay: ST77922 QSPI Driver (matching Install libraries/ST77922/ST77922.cpp)
// ============================================================================

constexpr size_t kQspiMaxTxPixels = 0x4000;  // TX_LEN (16384 pixels = 32768 bytes)
constexpr uint32_t kWrRamContinueCmd = 0x3C; // WR_RAM_C_CMD

void ES3C35PDisplay::txParam(int lcd_cmd, const void* param, size_t param_size) {
  if (!spi_dev_) return;
  GPIO.out_w1tc = (1U << PinLcdCs);  // LCD_CS_LOW
  spi_transaction_ext_t qspit = {};
  qspit.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
  qspit.base.cmd = QspiOpcodeWriteCmd;
  qspit.base.addr = (static_cast<uint32_t>(lcd_cmd) & 0xFFU) << 8;
  qspit.command_bits = 8;
  qspit.address_bits = 24;
  if (param_size != 0 && param != nullptr) {
    qspit.base.tx_buffer = param;
    qspit.base.length = 8 * param_size;
  } else {
    qspit.base.tx_buffer = nullptr;
    qspit.base.length = 0;
  }
  spi_device_polling_transmit(spi_dev_, reinterpret_cast<spi_transaction_t*>(&qspit));
  GPIO.out_w1ts = (1U << PinLcdCs);  // LCD_CS_HIGH
}

void ES3C35PDisplay::txColor(const uint16_t* data, size_t pixel_count) {
  if (!spi_dev_ || !data || pixel_count == 0) return;

  bool first_chunk = true;
  size_t remaining = pixel_count;
  const uint16_t* tx_buf = data;
  spi_transaction_ext_t espit = {};

  GPIO.out_w1tc = (1U << PinLcdCs);  // LCD_CS_LOW
  do {
    if (first_chunk) {
      espit.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
      espit.base.cmd = QspiOpcodeWriteColor;
      espit.base.addr = kWrRamContinueCmd << 8;
      espit.command_bits = 8;
      espit.address_bits = 24;
      first_chunk = false;
    } else {
      espit.base.flags =
          SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
    }
    const size_t tx_len = (remaining > kQspiMaxTxPixels) ? kQspiMaxTxPixels : remaining;
    espit.base.tx_buffer = tx_buf;
    espit.base.length = tx_len * 16;
    spi_device_polling_transmit(spi_dev_, reinterpret_cast<spi_transaction_t*>(&espit));
    remaining -= tx_len;
    tx_buf += tx_len;
  } while (remaining > 0);
  GPIO.out_w1ts = (1U << PinLcdCs);  // LCD_CS_HIGH
}

bool ES3C35PDisplay::init() {
  Serial.println("ES3C35P: Initializing QSPI bus on SPI2_HOST...");

  pinMode(PinLcdCs, OUTPUT);
  digitalWrite(PinLcdCs, HIGH);

  spi_bus_config_t buscfg = {};
  buscfg.data0_io_num = PinLcdD0;
  buscfg.data1_io_num = PinLcdD1;
  buscfg.sclk_io_num = PinLcdClk;
  buscfg.data2_io_num = PinLcdD2;
  buscfg.data3_io_num = PinLcdD3;
  buscfg.max_transfer_sz = (kQspiMaxTxPixels * 16) + 8;
  buscfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_IOMUX_PINS | SPICOMMON_BUSFLAG_QUAD;

  esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("ES3C35P: spi_bus_initialize failed: 0x%x\n", err);
    return false;
  }

  spi_device_interface_config_t devcfg = {};
  devcfg.command_bits = 0;
  devcfg.address_bits = 0;
  devcfg.mode = SPI_MODE0;
  devcfg.clock_speed_hz = 80000000;
  devcfg.spics_io_num = PinLcdCs;
  devcfg.flags = SPI_DEVICE_HALFDUPLEX;
  devcfg.queue_size = 17;

  err = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_dev_);
  if (err != ESP_OK) {
    Serial.printf("ES3C35P: spi_bus_add_device failed: 0x%x\n", err);
    return false;
  }

  // Run official ST77922 initialization table
  for (size_t i = 0; i < sizeof(kSt77922InitCmds) / sizeof(kSt77922InitCmds[0]); ++i) {
    txParam(kSt77922InitCmds[i].cmd, kSt77922InitCmds[i].data, kSt77922InitCmds[i].len);
    if (kSt77922InitCmds[i].delay_ms > 0) {
      delay(kSt77922InitCmds[i].delay_ms);
    }
  }

  // Set MADCTL = 0x00 (matching ST77922::Set_Rotation(1))
  const uint8_t madctl_val = 0x00;
  txParam(LCD_CMD_MADCTL, &madctl_val, 1);

  // Configure PWM backlight on GPIO 41 (Active HIGH via Q4 BSS138 MOSFET)
  ledcSetup(BacklightPwmCh, 20000, 8);
  ledcAttachPin(PinLcdBacklight, BacklightPwmCh);
  setBrightness(255);

  Serial.println("ES3C35P: ST77922 QSPI Display initialized");
  return true;
}

void ES3C35PDisplay::setBrightness(uint8_t brightness) {
  ledcWrite(BacklightPwmCh, brightness);
}

void ES3C35PDisplay::drawBitmap(int x_start, int y_start, int x_end, int y_end, const void* color_data) {
  if (!spi_dev_ || x_start >= x_end || y_start >= y_end) return;

  const uint8_t caset[4] = {
      static_cast<uint8_t>((x_start >> 8) & 0xFF),
      static_cast<uint8_t>(x_start & 0xFF),
      static_cast<uint8_t>(((x_end - 1) >> 8) & 0xFF),
      static_cast<uint8_t>((x_end - 1) & 0xFF),
  };
  const uint8_t raset[4] = {
      static_cast<uint8_t>((y_start >> 8) & 0xFF),
      static_cast<uint8_t>(y_start & 0xFF),
      static_cast<uint8_t>(((y_end - 1) >> 8) & 0xFF),
      static_cast<uint8_t>((y_end - 1) & 0xFF),
  };

  txParam(LCD_CMD_CASET, caset, sizeof(caset));
  txParam(LCD_CMD_RASET, raset, sizeof(raset));
  const size_t pixel_count = static_cast<size_t>(x_end - x_start) * static_cast<size_t>(y_end - y_start);
  txColor(reinterpret_cast<const uint16_t*>(color_data), pixel_count);
}

void ES3C35PDisplay::fillScreen(uint16_t color565) {
  constexpr int ChunkLines = 20;
  const size_t pixels = NativeWidth * ChunkLines;
  auto* buf = static_cast<uint16_t*>(heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!buf) return;
  for (size_t i = 0; i < pixels; ++i) buf[i] = color565;
  for (int y = 0; y < NativeHeight; y += ChunkLines) {
    drawBitmap(0, y, NativeWidth, y + ChunkLines, buf);
  }
  heap_caps_free(buf);
}

// ============================================================================
// ST77922Touch: Incell Capacitive Touch Driver (I2C Address 0x55)
// ============================================================================

bool ST77922Touch::begin(TwoWire& wire) {
  wire_ = &wire;

  if (PinTouchInt >= 0) {
    pinMode(PinTouchInt, INPUT);
  }

  // Hardware reset pulse on GPIO 48 matching official ST77922_Touch.cpp (LOW 100ms, HIGH 100ms)
  pinMode(PinTouchRst, OUTPUT);
  digitalWrite(PinTouchRst, HIGH);
  delay(10);
  digitalWrite(PinTouchRst, LOW);
  delay(100);
  digitalWrite(PinTouchRst, HIGH);
  delay(100);

  // Wait for STATUS (0x0001) lower nibble to clear (with bounded timeout)
  uint8_t status = 0;
  for (int retry = 0; retry < 20; ++retry) {
    if (readRegister(0x0001, &status, 1) && (status & 0x0F) == 0) {
      break;
    }
    delay(10);
  }

  uint8_t max_pts = 0;
  if (readRegister(0x0009, &max_pts, 1)) {
    if (max_pts > 0 && max_pts <= 10) {
      max_points_ = max_pts;
    }
    initialized_ = true;
    Serial.printf("ST77922Touch: detected at 0x%02X (max_points=%u)\n", TouchI2cAddr, max_points_);
    return true;
  }

  // Fallback check on 0x0010 info register
  uint8_t info = 0;
  if (readRegister(0x0010, &info, 1)) {
    initialized_ = true;
    Serial.printf("ST77922Touch: detected at 0x%02X via info reg\n", TouchI2cAddr);
    return true;
  }

  Serial.println("ST77922Touch: no touch response at 0x55");
  return false;
}

bool ST77922Touch::readRegister(uint16_t reg, uint8_t* data, size_t length) {
  if (!wire_ || !data || !length) return false;

  wire_->beginTransmission(TouchI2cAddr);
  wire_->write(static_cast<uint8_t>(reg >> 8));
  wire_->write(static_cast<uint8_t>(reg & 0xFF));
  if (wire_->endTransmission(false) != 0) return false;

  const size_t received = wire_->requestFrom(
      TouchI2cAddr, static_cast<uint8_t>(length), static_cast<uint8_t>(1));
  if (received != length) return false;
  for (size_t i = 0; i < length; ++i) {
    data[i] = wire_->read();
  }
  return true;
}

bool ST77922Touch::read(uint16_t& x, uint16_t& y) {
  if (!initialized_) return false;

  uint8_t info = 0;
  if (!readRegister(0x0010, &info, 1)) {
    x = last_x_;
    y = last_y_;
    return last_pressed_;
  }

  if (info & 0x08) {
    uint8_t buf[7 * 10] = {};
    const uint8_t pts_to_read = (max_points_ > 10) ? 10 : max_points_;
    const size_t read_len = 7U * pts_to_read;
    if (readRegister(0x0014, buf, read_len)) {
      for (uint8_t i = 0; i < pts_to_read; ++i) {
        if (buf[i * 7] & 0x80) {
          const uint16_t raw_x = static_cast<uint16_t>(((buf[i * 7] & 0x3F) << 8) | buf[i * 7 + 1]);
          const uint16_t raw_y = static_cast<uint16_t>(((buf[i * 7 + 2] & 0x3F) << 8) | buf[i * 7 + 3]);
          if (raw_x < 320 && raw_y < 480) {
            // Map native Portrait (320x480) to Landscape (480x320) matching 90-degree rotation:
            // Landscape X (0..479) = raw_y
            // Landscape Y (0..319) = 319 - raw_x
            const uint16_t lx = raw_y;
            const uint16_t ly = 319 - raw_x;

            const int16_t dx = static_cast<int16_t>(lx) - static_cast<int16_t>(last_x_);
            const int16_t dy = static_cast<int16_t>(ly) - static_cast<int16_t>(last_y_);
            if (!last_pressed_ || dx > 3 || dx < -3 || dy > 3 || dy < -3) {
              last_x_ = lx;
              last_y_ = ly;
            }
            last_pressed_ = true;
            last_sample_at_ = millis();
            x = last_x_;
            y = last_y_;
            return true;
          }
        }
      }
    }
  }

  last_pressed_ = false;
  x = last_x_;
  y = last_y_;
  return false;
}

// ============================================================================
// ES8311Audio: Onboard ES8311 Codec + FM8002E PA Speaker Driver
// ============================================================================

bool ES8311Audio::writeReg(uint8_t reg, uint8_t val) {
  if (!wire_) return false;
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  wire_->write(val);
  return wire_->endTransmission() == 0;
}

bool ES8311Audio::readReg(uint8_t reg, uint8_t& val) {
  if (!wire_) return false;
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(addr_, static_cast<uint8_t>(1)) != 1) return false;
  val = wire_->read();
  return true;
}

bool ES8311Audio::begin(TwoWire& wire, uint8_t initial_volume) {
  wire_ = &wire;
  volume_ = initial_volume > 100 ? 100 : initial_volume;

  // Keep SC8002B/FM8002E PA muted (HIGH = shutdown) until codec & I2S are ready
  pinMode(PinAudioPa, OUTPUT);
  digitalWrite(PinAudioPa, HIGH);

  // Probe ES8311 at 0x18 (or 0x19)
  uint8_t dummy = 0;
  if (readReg(0xFD, dummy)) {
    addr_ = 0x18;
  } else {
    addr_ = 0x19;
    if (!readReg(0xFD, dummy)) {
      Serial.println("ES8311: codec not found on I2C bus (board may be ES3C35P-NS)");
      return false;
    }
  }

  // Configure I2S0 peripheral at 24000 Hz, 16-bit Stereo, MCLK = 256 * 24000 = 6.144 MHz
  i2s_config_t i2s_cfg = {};
  i2s_cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_cfg.sample_rate = 24000;
  i2s_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2s_cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2s_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s_cfg.dma_buf_count = 6;
  i2s_cfg.dma_buf_len = 240;
  i2s_cfg.use_apll = false;
  i2s_cfg.tx_desc_auto_clear = true;
  i2s_cfg.fixed_mclk = 24000 * 256;

  if (i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, nullptr) != ESP_OK) {
    Serial.println("ES8311: i2s_driver_install failed");
    return false;
  }

  i2s_pin_config_t pin_cfg = {};
  pin_cfg.mck_io_num = PinI2sMclk;
  pin_cfg.bck_io_num = PinI2sBclk;
  pin_cfg.ws_io_num = PinI2sWs;
  pin_cfg.data_out_num = PinI2sDout;
  pin_cfg.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(I2S_NUM_0, &pin_cfg);
  i2s_zero_dma_buffer(I2S_NUM_0);

  // Initialize ES8311 registers for 24kHz with 6.144MHz MCLK
  writeReg(0x00, 0x1F);  // Reset
  delay(20);
  writeReg(0x00, 0x00);
  writeReg(0x00, 0x80);  // Power-on state machine (CSM_ON)

  writeReg(0x01, 0x3F);  // Enable all clocks from MCLK pin
  writeReg(0x02, 0x00);  // pre_div = 1, pre_multi = 1x
  writeReg(0x03, 0x10);  // single speed, adc_osr = 0x10
  writeReg(0x04, 0x10);  // dac_osr = 0x10
  writeReg(0x05, 0x00);  // adc_div = 1, dac_div = 1
  writeReg(0x06, 0x03);  // bclk_div = 4
  writeReg(0x07, 0x00);  // lrck_h = 0x00
  writeReg(0x08, 0xFF);  // lrck_l = 0xFF (div = 256)

  writeReg(0x09, 0x0C);  // SDP In 16-bit I2S
  writeReg(0x0A, 0x0C);  // SDP Out 16-bit I2S

  writeReg(0x0D, 0x01);  // Power up analog circuitry
  writeReg(0x0E, 0x02);  // Enable analog PGA & modulator
  writeReg(0x12, 0x00);  // Power up DAC
  writeReg(0x13, 0x10);  // Enable output to HP drive
  writeReg(0x1C, 0x6A);  // ADC EQ bypass
  writeReg(0x37, 0x08);  // Bypass DAC equalizer

  initialized_ = true;
  setVolume(volume_);
  Serial.printf("ES8311: initialized at I2C 0x%02X (volume=%u%%)\n", addr_, volume_);
  return true;
}

void ES8311Audio::setVolume(uint8_t volume) {
  volume_ = volume > 100 ? 100 : volume;
  if (!initialized_) return;

  if (volume_ == 0) {
    writeReg(0x32, 0x00);
    writeReg(0x31, 0x60);  // Mute DAC
    digitalWrite(PinAudioPa, HIGH);  // Shutdown PA
  } else {
    // Map 1..100% into ES8311 DAC_REG32 range 140..255 (0xBF is 0dB, up to 0xFF is +32dB digital boost)
    const uint8_t reg32 = static_cast<uint8_t>(140U + (static_cast<uint16_t>(volume_) * 115U) / 100U);
    writeReg(0x31, 0x00);  // Unmute DAC
    writeReg(0x32, reg32);
    // Keep SC8002B/FM8002E PA enabled (LOW) so 1uF BYPASS cap stays charged for instant full-volume beeps
    digitalWrite(PinAudioPa, LOW);
  }
}

void ES8311Audio::playTone(uint16_t freq_hz, uint32_t duration_ms) {
  if (!initialized_ || volume_ == 0 || freq_hz == 0 || duration_ms == 0) return;

  constexpr uint32_t SampleRate = 24000;
  const uint32_t total_frames = (SampleRate * duration_ms) / 1000U;
  static int16_t chunk[240 * 2];  // Matches 1 DMA buffer (240 stereo frames)

  digitalWrite(PinAudioPa, LOW);

  uint32_t frames_sent = 0;
  const float omega = 2.0f * 3.14159265f * static_cast<float>(freq_hz) / static_cast<float>(SampleRate);
  const float peak_amp = 32000.0f * (static_cast<float>(volume_) / 100.0f);

  while (frames_sent < total_frames) {
    const uint32_t n = (total_frames - frames_sent > 240U) ? 240U : (total_frames - frames_sent);
    for (uint32_t i = 0; i < n; ++i) {
      const uint32_t sample_idx = frames_sent + i;
      // Smooth 2.5ms fade-in/out envelope to prevent clicks
      float env = 1.0f;
      if (sample_idx < 60U) env = static_cast<float>(sample_idx) / 60.0f;
      else if (total_frames - sample_idx < 60U) env = static_cast<float>(total_frames - sample_idx) / 60.0f;

      // Harmonic-rich trapezoidal buzzer wave (3x overdriven sine clamped to [-1, 1]) for maximum speaker loudness
      float s = sinf(omega * static_cast<float>(sample_idx)) * 3.0f;
      if (s > 1.0f) s = 1.0f;
      else if (s < -1.0f) s = -1.0f;

      const int16_t val = static_cast<int16_t>(s * peak_amp * env);
      chunk[i * 2]     = val;
      chunk[i * 2 + 1] = val;
    }
    size_t bytes_written = 0;
    i2s_write(I2S_NUM_0, chunk, n * 2 * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(100));
    frames_sent += n;
  }

  // Push 6 DMA buffers of silence so the entire tone drains out to the speaker before returning
  memset(chunk, 0, sizeof(chunk));
  size_t bytes_written = 0;
  for (int b = 0; b < 6; ++b) {
    i2s_write(I2S_NUM_0, chunk, sizeof(chunk), &bytes_written, pdMS_TO_TICKS(50));
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// ============================================================================
// Application
// ============================================================================

bool Application::allocateDrawBuffers() {
  constexpr uint16_t BufferLines = 40;
  const size_t pixels = static_cast<size_t>(ES3C35PDisplay::ScreenWidth) * BufferLines;
  const size_t bytes = pixels * sizeof(lv_color_t);

  // Allocate DMA rotation scratch buffer in internal SRAM for QSPI hardware DMA
  rot_buffer_ = static_cast<uint16_t*>(
      heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!rot_buffer_) {
    Serial.println("LVGL: failed to allocate internal DMA rotation buffer");
    return false;
  }

  // Allocate LVGL render buffers (PSRAM or internal RAM)
  uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < 2 * bytes) {
    caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  }

  draw_buffer_[0] = static_cast<lv_color_t*>(heap_caps_malloc(bytes, caps));
  draw_buffer_[1] = static_cast<lv_color_t*>(heap_caps_malloc(bytes, caps));
  if (draw_buffer_[0] && draw_buffer_[1]) {
    draw_buffer_pixels_ = pixels;
    Serial.printf("LVGL: 2 x %u-line draw buffers + DMA rot buffer (%u bytes each), free SRAM=%u, free PSRAM=%u\n",
                  BufferLines, static_cast<unsigned>(bytes),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return true;
  }

  return false;
}

bool Application::begin(uint8_t initial_brightness, uint8_t initial_volume) {
  instance_ = this;

  // Initialize shared onboard I2C bus (SDA = IO38, SCL = IO39) for Touch and ES8311 Audio
  Wire.begin(PinI2cSda, PinI2cScl, 400000U);

  if (!display_.init()) return false;
  display_.setBrightness(initial_brightness);
  display_.fillScreen(0x0000);

  touch_.begin(Wire);
  audio_.begin(Wire, initial_volume);

  if (!allocateDrawBuffers()) return false;

  lv_init();
  lv_disp_draw_buf_init(&draw_buffer_descriptor_, draw_buffer_[0], draw_buffer_[1], draw_buffer_pixels_);
  lv_disp_drv_init(&display_driver_);
  display_driver_.hor_res = ES3C35PDisplay::ScreenWidth;
  display_driver_.ver_res = ES3C35PDisplay::ScreenHeight;
  display_driver_.flush_cb = flush;
  display_driver_.rounder_cb = rounder;
  display_driver_.draw_buf = &draw_buffer_descriptor_;
  lv_disp_drv_register(&display_driver_);

  lv_indev_drv_init(&touch_driver_);
  touch_driver_.type = LV_INDEV_TYPE_POINTER;
  touch_driver_.read_cb = readTouch;
  lv_indev_drv_register(&touch_driver_);

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x101418), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
  return true;
}

void Application::update() {
  static uint32_t last_tick = millis();
  const uint32_t now = millis();
  lv_tick_inc(now - last_tick);
  last_tick = now;
  lv_timer_handler();
  delay(1);
}

void Application::brightness(uint8_t value) {
  display_.setBrightness(value);
}

void Application::volume(uint8_t value) {
  audio_.setVolume(value);
}

void Application::playSpeakerBeep(uint16_t freq_hz, uint32_t duration_ms) {
  audio_.playTone(freq_hz, duration_ms);
}

void Application::rounder(lv_disp_drv_t*, lv_area_t* area) {
  // Align Landscape Y (0..319) to 4-pixel boundaries so Portrait X (319 - ly) is 4-pixel aligned for ST77922
  area->y1 = area->y1 & ~3;
  area->y2 = (area->y2 | 3);
}

// Rotate Landscape LVGL area [x1..x2, y1..y2] (480x320) into Portrait ST77922 area (320x480)
// with 4-pixel X-alignment required by ST77922 controller.
// Mapping: Portrait px = 319 - ly, Portrait py = lx
void Application::flush(lv_disp_drv_t* lv_display, const lv_area_t* area, lv_color_t* pixels) {
  if (!instance_ || !instance_->rot_buffer_) {
    lv_disp_flush_ready(lv_display);
    return;
  }

  const int32_t lx1 = area->x1;
  const int32_t lx2 = area->x2;
  const int32_t ly1 = area->y1;
  const int32_t ly2 = area->y2;
  const int32_t lw = lx2 - lx1 + 1;

  // Portrait bounds before 4-pixel rounding:
  // px_raw1 = 319 - ly2, px_raw2 = 319 - ly1
  // Align px1 down to multiple of 4, px2 up to multiple of 4 minus 1:
  const int32_t px1 = ((319 - ly2) & ~3);
  const int32_t px2 = (((319 - ly1) + 4) & ~3) - 1;
  const int32_t py1 = lx1;
  const int32_t py2 = lx2;

  const int32_t pw = px2 - px1 + 1;  // Guaranteed multiple of 4
  const int32_t ph = py2 - py1 + 1;  // == lw

  const auto* src = reinterpret_cast<const uint16_t*>(pixels);
  uint16_t* dst = instance_->rot_buffer_;

  // Since ly2 = 319 - px1 and ly1 = 319 - px2 (due to rounder or exact match):
  // If LVGL didn't align ly1/ly2 to 4, clamp source y to [ly1..ly2]
  for (int32_t py = py1; py <= py2; ++py) {
    const int32_t lx = py - lx1;
    uint16_t* dst_row = dst + (py - py1) * pw;
    for (int32_t px = px1; px <= px2; ++px) {
      int32_t ly = 319 - px;
      if (ly < ly1) ly = ly1;
      else if (ly > ly2) ly = ly2;
      dst_row[px - px1] = src[(ly - ly1) * lw + lx];
    }
  }

  instance_->display_.drawBitmap(px1, py1, px2 + 1, py2 + 1, dst);
  lv_disp_flush_ready(lv_display);
}

void Application::readTouch(lv_indev_drv_t*, lv_indev_data_t* data) {
  if (!instance_) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  uint16_t x = 0;
  uint16_t y = 0;
  if (instance_->touch_.read(x, y)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
    instance_->touch_x_ = x;
    instance_->touch_y_ = y;
    instance_->last_touch_at_ = millis();
    if (!instance_->touching_) {
      ++instance_->touch_sequence_;
      Serial.printf("Touch: x=%u, y=%u\n", x, y);
    }
    instance_->touching_ = true;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
    instance_->touching_ = false;
  }
}

}  // namespace washerui
