#include "WasherView.h"
#include "AppConfig.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <time.h>

using namespace washerui;

WasherView* WasherView::instance_ = nullptr;

namespace {

lv_obj_t* createWasherMachine(lv_obj_t* parent, int x, int y, int width, int height,
                              uint32_t bodyColor, lv_obj_t** drum) {
  lv_obj_t* body = lv_obj_create(parent);
  lv_obj_set_size(body, width, height);
  lv_obj_set_pos(body, x, y);
  lv_obj_set_style_radius(body, 12, 0);
  lv_obj_set_style_bg_color(body, lv_color_hex(bodyColor), 0);
  lv_obj_set_style_border_width(body, 2, 0);
  lv_obj_set_style_border_color(body, lv_color_hex(0xD5E1E8), 0);
  lv_obj_set_style_shadow_width(body, 12, 0);
  lv_obj_set_style_shadow_opa(body, LV_OPA_20, 0);
  lv_obj_set_style_shadow_ofs_y(body, 5, 0);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* panel = lv_obj_create(body);
  lv_obj_set_size(panel, width - 16, 22);
  lv_obj_set_pos(panel, 8, 2);
  lv_obj_set_style_radius(panel, 5, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x15394E), 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* light = lv_obj_create(panel);
  lv_obj_set_size(light, 8, 8);
  lv_obj_align(light, LV_ALIGN_RIGHT_MID, -7, 0);
  lv_obj_set_style_radius(light, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(light, lv_color_hex(0x55E39A), 0);
  lv_obj_set_style_border_width(light, 0, 0);

  *drum = lv_arc_create(body);
  const int drumSize = width - 54;
  lv_obj_set_size(*drum, drumSize, drumSize);
  lv_obj_align(*drum, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_arc_set_bg_angles(*drum, 0, 360);
  lv_arc_set_angles(*drum, 20, 145);
  lv_obj_set_style_arc_width(*drum, 12, LV_PART_MAIN);
  lv_obj_set_style_arc_color(*drum, lv_color_hex(0xB9CED9), LV_PART_MAIN);
  lv_obj_set_style_arc_width(*drum, 12, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(*drum, lv_color_hex(0x18A7D6), LV_PART_INDICATOR);
  lv_obj_remove_style(*drum, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(*drum, LV_OBJ_FLAG_CLICKABLE);
  return body;
}

lv_obj_t* createDot(lv_obj_t* parent, int size, uint32_t color) {
  lv_obj_t* dot = lv_obj_create(parent);
  lv_obj_set_size(dot, size, size);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(dot, 2, 0);
  lv_obj_set_style_border_color(dot, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_shadow_width(dot, 8, 0);
  lv_obj_set_style_shadow_opa(dot, LV_OPA_30, 0);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  return dot;
}

void popupBounceAnimCb(void* var, int32_t v) {
  lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(var), v, 0);
}

void triggerPopupBounce(lv_obj_t* popup) {
  if (!popup) return;
  lv_anim_del(popup, popupBounceAnimCb);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, popup);
  lv_anim_set_values(&a, 28, 0);
  lv_anim_set_time(&a, 340);
  lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
  lv_anim_set_exec_cb(&a, popupBounceAnimCb);
  lv_anim_start(&a);
}

float distToSegment(float px, float py, float ax, float ay, float bx, float by) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float wx = px - ax;
  const float wy = py - ay;
  const float c1 = wx * vx + wy * vy;
  if (c1 <= 0.0f) return hypotf(px - ax, py - ay);
  const float c2 = vx * vx + vy * vy;
  if (c2 <= c1) return hypotf(px - bx, py - by);
  const float b = c1 / c2;
  return hypotf(px - (ax + b * vx), py - (ay + b * vy));
}

uint8_t sdfAlpha(float signedDist) {
  // signedDist < 0 means inside shape; smooth 1.4px anti-aliased edge
  if (signedDist <= -0.7f) return 255;
  if (signedDist >= 0.7f) return 0;
  return static_cast<uint8_t>((0.7f - signedDist) * (255.0f / 1.4f));
}

void createTempBadgeIcon(lv_obj_t* btn, uint8_t opt) {
  // Smart Home Water Wave Card (138x162 px interior of 142x166 button)
  // Top half is completely clean for crisp typography; bottom half features Double Layered Water Waves,
  // splashing mini droplets, and rising glossy water bubbles clipped to the button's rounded corners.
  constexpr int kW = 138;
  constexpr int kH = 162;
  static uint8_t* iconBuf[3] = {nullptr, nullptr, nullptr};
  static lv_img_dsc_t iconDsc[3];
  static bool initialized = false;

  if (!initialized) {
    initialized = true;
    const float cx = (kW - 1) * 0.5f;
    const float cy = (kH - 1) * 0.5f;
    const float cornerR = 10.0f;

    const struct {
      // Back wave (lighter luminous layer)
      uint8_t w1R, w1G, w1B;
      // Front wave gradient (vivid top to rich bottom)
      uint8_t w2TopR, w2TopG, w2TopB;
      uint8_t w2BotR, w2BotG, w2BotB;
      // Foam crest highlight color
      uint8_t crR, crG, crB;
      // Wave phase offsets so each card feels lively
      float phase1, phase2;
    } pal[3] = {
      // 1: COLD (Button #1A8CFF) -> Ice-Aqua back wave + Deep Azure front wave
      {0x68, 0xBC, 0xFF,  0x05, 0x72, 0xEB,  0x00, 0x51, 0xC4,  0xBF, 0xE4, 0xFF,  0.4f, 1.1f},
      // 2: WARM (Button #FEBD3B) -> Sunny-Cream back wave + Vivid Amber front wave
      {0xFF, 0xDA, 0x7A,  0xF5, 0x9E, 0x0B,  0xD9, 0x77, 0x06,  0xFF, 0xF0, 0xC4,  1.2f, 0.3f},
      // 3: HOT  (Button #FF5522) -> Coral-Peach back wave + Vivid Vermilion front wave
      {0xFF, 0x8C, 0x66,  0xEC, 0x3A, 0x00,  0xC6, 0x22, 0x00,  0xFF, 0xD2, 0xC2,  0.0f, 0.8f}
    };

    const struct {
      float bx, by, br;
    } bubbles[4] = {
      {30.0f, 136.0f, 5.5f},
      {58.0f, 147.0f, 3.6f},
      {88.0f, 132.0f, 6.5f},
      {112.0f, 144.0f, 4.2f}
    };

    // Helper lambda for mini splash droplet SDF
    auto dropletSdf = [](float px, float py, float dcx, float dTipY, float dJoinY, float dRx, float dRy) -> float {
      const float dx = px - dcx;
      if (py < dTipY) return hypotf(dx, py - dTipY);
      if (py <= dJoinY) {
        const float u = (py - dTipY) / (dJoinY - dTipY);
        const float halfW = dRx * powf(sinf(u * 1.5707963f), 1.15f);
        return (fabsf(dx) - halfW) * 0.88f;
      }
      const float dy = py - dJoinY;
      return (hypotf(dx / dRx, dy / dRy) - 1.0f) * dRy;
    };

    for (uint8_t o = 1; o <= 3; ++o) {
      const size_t bufBytes = static_cast<size_t>(kW) * kH * LV_IMG_PX_SIZE_ALPHA_BYTE;
      uint8_t* dst = static_cast<uint8_t*>(heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!dst) {
        dst = static_cast<uint8_t*>(malloc(bufBytes));
      }
      iconBuf[o - 1] = dst;
      if (!dst) continue;

      const auto& p = pal[o - 1];
      for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
          // Rounded rectangle clip mask matching button interior
          const float qx = fabsf(x - cx) - (kW * 0.5f - cornerR);
          const float qy = fabsf(y - cy) - (kH * 0.5f - cornerR);
          const float boxDist = hypotf(fmaxf(qx, 0.0f), fmaxf(qy, 0.0f)) + fminf(fmaxf(qx, qy), 0.0f) - cornerR;

          uint8_t outAlpha = 0;
          uint8_t rCol = p.w1R;
          uint8_t gCol = p.w1G;
          uint8_t bCol = p.w1B;

          if (boxDist < 0.7f && y >= 72) {
            const uint8_t clipCov = sdfAlpha(boxDist);

            // Wave 1 (back wave: higher, lighter luminous crest)
            const float yW1 = 96.5f + 6.5f * sinf(x * 0.048f + p.phase1) + 2.0f * cosf(x * 0.095f);
            const float dW1 = yW1 - y; // negative inside wave 1

            // Wave 2 (front wave: lower, deeper vivid gradient + foam crest)
            const float yW2 = 109.5f + 7.0f * cosf(x * 0.045f + p.phase2) + 2.2f * sinf(x * 0.090f);
            const float dW2 = yW2 - y; // negative inside wave 2

            float accAlpha = 0.0f;
            float accR = p.w1R, accG = p.w1G, accB = p.w1B;

            // 1) Back wave fill
            if (dW1 < 0.7f) {
              const float cov1 = sdfAlpha(dW1) / 255.0f;
              accAlpha = cov1 * 175.0f;
              accR = p.w1R;
              accG = p.w1G;
              accB = p.w1B;
            }

            // 2) Cute splashing mini water droplets just above the waves
            const float drop1 = dropletSdf(x, y, 110.0f, 75.0f, 85.0f, 5.2f, 5.2f);
            const float drop2 = dropletSdf(x, y, 25.0f, 81.0f, 88.5f, 3.8f, 3.8f);
            const float dDrop = fminf(drop1, drop2);
            if (dDrop < 0.7f) {
              const float dCov = sdfAlpha(dDrop) / 255.0f;
              const float dA = dCov * 210.0f;
              accR = p.crR;
              accG = p.crG;
              accB = p.crB;
              accAlpha = fmaxf(accAlpha, dA);
            }

            // 3) Front wave fill + foam crest
            if (dW2 < 0.7f) {
              const float cov2 = sdfAlpha(dW2) / 255.0f;
              float wt = (y - 102.0f) / (kH - 102.0f);
              if (wt < 0.0f) wt = 0.0f;
              if (wt > 1.0f) wt = 1.0f;

              float fR = p.w2TopR + (static_cast<int>(p.w2BotR) - p.w2TopR) * wt;
              float fG = p.w2TopG + (static_cast<int>(p.w2BotG) - p.w2TopG) * wt;
              float fB = p.w2TopB + (static_cast<int>(p.w2BotB) - p.w2TopB) * wt;

              // Bright foam crest along top edge of front wave (1.6px)
              const float crestDist = fabsf(dW2 + 0.8f) - 0.8f;
              if (crestDist < 0.7f) {
                const float cMix = (sdfAlpha(crestDist) / 255.0f) * 0.75f;
                fR = fR * (1.0f - cMix) + p.crR * cMix;
                fG = fG * (1.0f - cMix) + p.crG * cMix;
                fB = fB * (1.0f - cMix) + p.crB * cMix;
              }

              // Rising glossy bubbles inside front wave
              for (int b = 0; b < 4; ++b) {
                const float bDist = hypotf(x - bubbles[b].bx, y - bubbles[b].by) - bubbles[b].br;
                if (bDist < 0.7f) {
                  const float bFillCov = sdfAlpha(bDist) / 255.0f;
                  const float bRimCov = sdfAlpha(fabsf(bDist + 0.65f) - 0.65f) / 255.0f;
                  const float wMix = bFillCov * 0.20f + bRimCov * 0.55f;
                  fR = fR * (1.0f - wMix) + 255.0f * wMix;
                  fG = fG * (1.0f - wMix) + 255.0f * wMix;
                  fB = fB * (1.0f - wMix) + 255.0f * wMix;
                }
              }

              const float frontA = 238.0f;
              accR = accR * (1.0f - cov2) + fR * cov2;
              accG = accG * (1.0f - cov2) + fG * cov2;
              accB = accB * (1.0f - cov2) + fB * cov2;
              accAlpha = accAlpha * (1.0f - cov2) + frontA * cov2;
            }

            rCol = static_cast<uint8_t>(accR);
            gCol = static_cast<uint8_t>(accG);
            bCol = static_cast<uint8_t>(accB);
            outAlpha = static_cast<uint8_t>(( static_cast<uint16_t>(accAlpha) * clipCov) / 255U);
          }

          const lv_color_t c = lv_color_make(rCol, gCol, bCol);
          const auto* cBytes = reinterpret_cast<const uint8_t*>(&c);
          const size_t idx = static_cast<size_t>((y * kW + x) * 3);
          dst[idx + 0] = cBytes[0];
          dst[idx + 1] = cBytes[1];
          dst[idx + 2] = outAlpha;
        }
      }

      iconDsc[o - 1].header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
      iconDsc[o - 1].header.always_zero = 0;
      iconDsc[o - 1].header.reserved = 0;
      iconDsc[o - 1].header.w = kW;
      iconDsc[o - 1].header.h = kH;
      iconDsc[o - 1].data_size = bufBytes;
      iconDsc[o - 1].data = dst;
    }
  }

  const uint8_t idx = (opt >= 1 && opt <= 3) ? (opt - 1) : 0;
  if (!iconBuf[idx]) return;
  lv_obj_t* img = lv_img_create(btn);
  lv_img_set_src(img, &iconDsc[idx]);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
}

volatile bool g_bootChimeDone = true;

void bootChimeTask(void* /*param*/) {
  // Unhurried 5-note startup chime on Core 0 (6144-byte stack) synced with E - P - A - Y reveal
  vTaskDelay(pdMS_TO_TICKS(250));
  washerui::App.playSpeakerBeep(523, 95);   // C5 ("E" at 250ms)
  vTaskDelay(pdMS_TO_TICKS(245));
  washerui::App.playSpeakerBeep(659, 95);   // E5 ("P" at 650ms)
  vTaskDelay(pdMS_TO_TICKS(245));
  washerui::App.playSpeakerBeep(784, 95);   // G5 ("A" at 1050ms)
  vTaskDelay(pdMS_TO_TICKS(245));
  washerui::App.playSpeakerBeep(1047, 130); // C6 ("Y" at 1450ms)
  vTaskDelay(pdMS_TO_TICKS(60));
  washerui::App.playSpeakerBeep(1319, 220); // E6 (Warm harmonic finish)
  g_bootChimeDone = true;
  vTaskDelete(nullptr);
}

}  // namespace

void WasherView::begin(AppSettings& settings, WasherViewListener& listener) {
  instance_ = this;
  settings_ = &settings;
  listener_ = &listener;
  buildPages();
  buildPinPage();
  buildSettings();
  buildWifiScanPage();
  show(Page::Home);
  playBootSplash();
  imageSwapAt_ = millis() + 4000;
}

void WasherView::playBootSplash() {
  lv_obj_t* topLayer = lv_layer_top();
  lv_obj_t* splash = lv_obj_create(topLayer);
  lv_obj_set_size(splash, 480, 320);
  lv_obj_set_pos(splash, 0, 0);
  lv_obj_set_style_radius(splash, 0, 0);
  lv_obj_set_style_border_width(splash, 0, 0);
  lv_obj_set_style_pad_all(splash, 0, 0);
  lv_obj_set_style_bg_color(splash, lv_color_hex(0x081526), 0);
  lv_obj_set_style_bg_grad_color(splash, lv_color_hex(0x0E2747), 0);
  lv_obj_set_style_bg_grad_dir(splash, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
  lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE);

  // Subtle decorative top & bottom frame lines (never overlapping text or cards)
  lv_obj_t* topAccent = lv_obj_create(splash);
  lv_obj_set_size(topAccent, 480, 4);
  lv_obj_align(topAccent, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(topAccent, 0, 0);
  lv_obj_set_style_border_width(topAccent, 0, 0);
  lv_obj_set_style_bg_color(topAccent, lv_color_hex(0x00E5FF), 0);
  lv_obj_set_style_bg_grad_color(topAccent, lv_color_hex(0x10B981), 0);
  lv_obj_set_style_bg_grad_dir(topAccent, LV_GRAD_DIR_HOR, 0);
  lv_obj_clear_flag(topAccent, LV_OBJ_FLAG_SCROLLABLE);

  // Zone 1 (y = 44..132): 4 clean 3D letter cards [ E ] [ P ] [ A ] [ Y ] with zero background clutter
  const char* letters[4] = {"E", "P", "A", "Y"};
  const uint32_t tileBgTop[4] = {0xFF5522, 0x1A8CFF, 0x10B981, 0xFEBD3B};
  const uint32_t tileBgBot[4] = {0xD93400, 0x005EC4, 0x059669, 0xD97706};
  lv_obj_t* tiles[4] = {};
  lv_obj_t* dots[4] = {};
  bool tileShown[4] = {false, false, false, false};

  for (int i = 0; i < 4; ++i) {
    tiles[i] = lv_obj_create(splash);
    lv_obj_set_size(tiles[i], 76, 86);
    lv_obj_set_pos(tiles[i], 72 + i * 88, 42);
    lv_obj_set_style_radius(tiles[i], 16, 0);
    lv_obj_set_style_bg_color(tiles[i], lv_color_hex(tileBgTop[i]), 0);
    lv_obj_set_style_bg_grad_color(tiles[i], lv_color_hex(tileBgBot[i]), 0);
    lv_obj_set_style_bg_grad_dir(tiles[i], LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(tiles[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tiles[i], 2, 0);
    lv_obj_set_style_border_color(tiles[i], lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(tiles[i], LV_OPA_80, 0);
    lv_obj_set_style_pad_all(tiles[i], 0, 0);
    lv_obj_clear_flag(tiles[i], LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(tiles[i]);
    lv_label_set_text(lbl, letters[i]);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);

    lv_obj_add_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);

    // 4 indicator dots at the bottom that light up with each letter
    dots[i] = lv_obj_create(splash);
    lv_obj_set_size(dots[i], 10, 10);
    lv_obj_set_pos(dots[i], 198 + i * 25, 286);
    lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dots[i], lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_border_width(dots[i], 0, 0);
    lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_SCROLLABLE);
  }

  // Zone 2 (y = 148..224): Dedicated clean typography zone (no background shapes behind text!)
  lv_obj_t* engSub = lv_label_create(splash);
  lv_label_set_text(engSub, "SMART LAUNDRY PAYMENT");
  lv_obj_set_style_text_font(engSub, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(engSub, lv_color_hex(0x38BDF8), 0);
  lv_obj_align(engSub, LV_ALIGN_TOP_MID, 0, 150);
  lv_obj_add_flag(engSub, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* divider = lv_obj_create(splash);
  lv_obj_set_size(divider, 220, 2);
  lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 180);
  lv_obj_set_style_bg_color(divider, lv_color_hex(0x1E4976), 0);
  lv_obj_set_style_border_width(divider, 0, 0);
  lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(divider, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* laoSub = lv_label_create(splash);
  lv_label_set_text(laoSub, shapeLaoText("ລະບົບຊຳລະເງິນເຄື່ອງຊັກຜ້າອັດຕະໂນມັດ").c_str());
  lv_obj_set_style_text_font(laoSub, &notosanslao_shaped_20, 0);
  lv_obj_set_style_text_color(laoSub, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(laoSub, LV_ALIGN_TOP_MID, 0, 190);
  lv_obj_add_flag(laoSub, LV_OBJ_FLAG_HIDDEN);

  // Zone 3 (y = 248..260): Smooth progress bar
  lv_obj_t* bar = lv_bar_create(splash);
  lv_obj_set_size(bar, 280, 10);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 250);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(bar, 5, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x163254), LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x00E5FF), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(bar, lv_color_hex(0x10B981), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);

  // Start non-blocking chime task on Core 0 with generous 6144-byte stack
  g_bootChimeDone = false;
  xTaskCreatePinnedToCore(bootChimeTask, "bootChime", 6144, nullptr, 2, nullptr, 0);

  const uint32_t startMs = millis();
  constexpr uint32_t kSplashDurationMs = 3600; // Unhurried 3.6 seconds total
  const uint32_t revealTimes[4] = {250, 650, 1050, 1450};
  bool subShown = false;

  while (true) {
    esp_task_wdt_reset();
    const uint32_t elapsed = millis() - startMs;
    if (elapsed >= kSplashDurationMs && g_bootChimeDone) {
      break;
    }

    // Pop in E - P - A - Y tiles one by one (every 400ms)
    for (int i = 0; i < 4; ++i) {
      if (!tileShown[i] && elapsed >= revealTimes[i]) {
        tileShown[i] = true;
        lv_obj_clear_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
        triggerPopupBounce(tiles[i]);
        lv_obj_set_style_bg_color(dots[i], lv_color_hex(tileBgTop[i]), 0);
      }
    }

    // Reveal clean subtitles below the cards after 'Y' appears
    if (!subShown && elapsed >= 1500) {
      subShown = true;
      lv_obj_clear_flag(engSub, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(divider, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(laoSub, LV_OBJ_FLAG_HIDDEN);
      triggerPopupBounce(engSub);
      triggerPopupBounce(laoSub);
    }

    int pct = static_cast<int>((elapsed * 100U) / 3000U);
    if (pct > 100) pct = 100;
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);

    washerui::App.update();
    delay(10);
  }

  // Clean up all splash objects so 0 bytes of RAM remain used after boot
  for (int i = 0; i < 4; ++i) {
    lv_anim_del(tiles[i], popupBounceAnimCb);
  }
  lv_anim_del(engSub, popupBounceAnimCb);
  lv_anim_del(laoSub, popupBounceAnimCb);
  lv_obj_del(splash);
}

void WasherView::configurePage(Rect& page, uint32_t color) {
  page.size(480, 320);
  page.position(0, 0);
  page.color(color);
  page.radius(0);
  page.border(0, color);
  page.padding(0, 0, 0, 0);
}

void WasherView::buildPages() {
  configurePage(home_, 0xD4EBFD); // Light blue background
  homeImage_.parent(home_);
  homeImage_.align(Align::Center, 0, 5);
  homeImage_.clickable(true).onClicked(optionClicked, nullptr);

  // Top-center live current time pill on Home page
  homeClockPill_ = lv_obj_create(home_.native());
  lv_obj_set_size(homeClockPill_, 86, 22);
  lv_obj_align(homeClockPill_, LV_ALIGN_TOP_MID, 0, 2);
  lv_obj_set_style_radius(homeClockPill_, 11, 0);
  lv_obj_set_style_bg_color(homeClockPill_, lv_color_hex(0x0B2239), 0);
  lv_obj_set_style_bg_opa(homeClockPill_, LV_OPA_50, 0);
  lv_obj_set_style_border_width(homeClockPill_, 1, 0);
  lv_obj_set_style_border_color(homeClockPill_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_opa(homeClockPill_, LV_OPA_50, 0);
  lv_obj_set_style_pad_all(homeClockPill_, 0, 0);
  lv_obj_clear_flag(homeClockPill_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(homeClockPill_, LV_OBJ_FLAG_CLICKABLE);

  homeClockLabel_ = lv_label_create(homeClockPill_);
  lv_label_set_text(homeClockLabel_, "--:--");
  lv_obj_set_style_text_font(homeClockLabel_, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(homeClockLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(homeClockLabel_);

  // Custom vector WiFi icon container (extreme top right corner, balanced & unclipped)
  lv_obj_t* topLayer = lv_layer_top();

  wifiCont_ = lv_obj_create(topLayer);
  lv_obj_set_size(wifiCont_, 20, 15);
  lv_obj_align(wifiCont_, LV_ALIGN_TOP_RIGHT, -3, 3);
  lv_obj_set_style_bg_opa(wifiCont_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wifiCont_, 0, 0);
  lv_obj_set_style_pad_all(wifiCont_, 0, 0);
  lv_obj_add_flag(wifiCont_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_clear_flag(wifiCont_, LV_OBJ_FLAG_SCROLLABLE);

  wifiDot_ = lv_obj_create(wifiCont_);
  lv_obj_set_size(wifiDot_, 3, 3);
  lv_obj_set_pos(wifiDot_, 9, 11);
  lv_obj_set_style_radius(wifiDot_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(wifiDot_, lv_color_hex(0xE85D55), 0); // Default red
  lv_obj_set_style_bg_opa(wifiDot_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(wifiDot_, 0, 0);
  lv_obj_clear_flag(wifiDot_, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t i = 0; i < 3; ++i) {
    wifiArcs_[i] = lv_arc_create(wifiCont_);
    const int size = 10 + i * 6; // Sizes: 10, 16, 22 (cleanly spaced concentric arcs around center 10,13)
    lv_obj_set_size(wifiArcs_[i], size, size);
    lv_obj_set_pos(wifiArcs_[i], 10 - (size / 2), 13 - (size / 2));
    lv_arc_set_bg_angles(wifiArcs_[i], 225, 315);
    lv_arc_set_angles(wifiArcs_[i], 225, 315);
    lv_obj_set_style_arc_width(wifiArcs_[i], 2, LV_PART_MAIN);
    lv_obj_set_style_arc_width(wifiArcs_[i], 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(wifiArcs_[i], lv_color_hex(0xE85D55), LV_PART_INDICATOR); // Default red
    lv_obj_set_style_arc_opa(wifiArcs_[i], LV_OPA_TRANSP, LV_PART_MAIN); // Hide background arc
    lv_obj_remove_style(wifiArcs_[i], nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(wifiArcs_[i], LV_OBJ_FLAG_CLICKABLE);
  }

  // Custom vector Globe icon (bottom-aligned at y=17 with WiFi dot, spaced cleanly to the left)
  netGlobe_ = lv_obj_create(topLayer);
  lv_obj_set_size(netGlobe_, 13, 13);
  lv_obj_align(netGlobe_, LV_ALIGN_TOP_RIGHT, -27, 4);
  lv_obj_set_style_radius(netGlobe_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(netGlobe_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(netGlobe_, 2, 0);
  lv_obj_set_style_border_color(netGlobe_, lv_color_hex(0xFFFFFF), 0); // Default white
  lv_obj_set_style_pad_all(netGlobe_, 0, 0);
  lv_obj_clear_flag(netGlobe_, LV_OBJ_FLAG_SCROLLABLE);

  netGlobeVLine_ = lv_obj_create(netGlobe_);
  lv_obj_set_size(netGlobeVLine_, 5, 11);
  lv_obj_align(netGlobeVLine_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(netGlobeVLine_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(netGlobeVLine_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(netGlobeVLine_, 1, 0);
  lv_obj_set_style_border_color(netGlobeVLine_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_clear_flag(netGlobeVLine_, LV_OBJ_FLAG_SCROLLABLE);

  netGlobeHLine_ = lv_obj_create(netGlobe_);
  lv_obj_set_size(netGlobeHLine_, 9, 1);
  lv_obj_align(netGlobeHLine_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(netGlobeHLine_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(netGlobeHLine_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(netGlobeHLine_, 0, 0);
  lv_obj_clear_flag(netGlobeHLine_, LV_OBJ_FLAG_SCROLLABLE);

  secretArea_ = lv_obj_create(topLayer);
  lv_obj_set_size(secretArea_, 50, 50);
  lv_obj_align(secretArea_, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_bg_opa(secretArea_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(secretArea_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(secretArea_, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(secretArea_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(secretArea_, secretEvent, LV_EVENT_ALL, this);

  // Dimmed Backdrop Overlay for top-layer popups (Reset & OTA)
  popupBackdrop_ = lv_obj_create(topLayer);
  lv_obj_set_size(popupBackdrop_, 480, 320);
  lv_obj_set_pos(popupBackdrop_, 0, 0);
  lv_obj_set_style_radius(popupBackdrop_, 0, 0);
  lv_obj_set_style_bg_color(popupBackdrop_, lv_color_hex(0x06101A), 0);
  lv_obj_set_style_bg_opa(popupBackdrop_, LV_OPA_60, 0);
  lv_obj_set_style_border_width(popupBackdrop_, 0, 0);
  lv_obj_clear_flag(popupBackdrop_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(popupBackdrop_, LV_OBJ_FLAG_HIDDEN);

  // Reset Screen Popup (shown when remote stop / screen reset command is received)
  resetPopup_ = lv_obj_create(topLayer);
  lv_obj_set_size(resetPopup_, 336, 176);
  lv_obj_align(resetPopup_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(resetPopup_, lv_color_hex(0x162536), 0);
  lv_obj_set_style_bg_grad_color(resetPopup_, lv_color_hex(0x0D1722), 0);
  lv_obj_set_style_bg_grad_dir(resetPopup_, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_color(resetPopup_, lv_color_hex(0x12B76A), 0);
  lv_obj_set_style_border_width(resetPopup_, 2, 0);
  lv_obj_set_style_radius(resetPopup_, 16, 0);
  lv_obj_set_style_shadow_width(resetPopup_, 32, 0);
  lv_obj_set_style_shadow_color(resetPopup_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(resetPopup_, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(resetPopup_, 0, 0);
  lv_obj_clear_flag(resetPopup_, LV_OBJ_FLAG_SCROLLABLE);

  // Animated spinning outer ring + inner emerald icon badge
  lv_obj_t* resetSpinner = lv_spinner_create(resetPopup_, 900, 75);
  lv_obj_set_size(resetSpinner, 62, 62);
  lv_obj_align(resetSpinner, LV_ALIGN_TOP_MID, 0, 14);
  lv_obj_set_style_arc_color(resetSpinner, lv_color_hex(0x12B76A), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(resetSpinner, lv_color_hex(0x22384D), LV_PART_MAIN);
  lv_obj_set_style_arc_width(resetSpinner, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(resetSpinner, 4, LV_PART_MAIN);

  lv_obj_t* resetBadge = lv_obj_create(resetPopup_);
  lv_obj_set_size(resetBadge, 44, 44);
  lv_obj_align(resetBadge, LV_ALIGN_TOP_MID, 0, 23);
  lv_obj_set_style_radius(resetBadge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(resetBadge, lv_color_hex(0x12B76A), 0);
  lv_obj_set_style_border_width(resetBadge, 0, 0);
  lv_obj_set_style_pad_all(resetBadge, 0, 0);
  lv_obj_clear_flag(resetBadge, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* resetIcon = lv_label_create(resetBadge);
  lv_label_set_text(resetIcon, LV_SYMBOL_REFRESH);
  lv_obj_set_style_text_color(resetIcon, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(resetIcon, &lv_font_montserrat_28, 0);
  lv_obj_center(resetIcon);

  lv_obj_t* resetTitle = lv_label_create(resetPopup_);
  lv_label_set_text(resetTitle, shapeLaoText("ຣີເຊັດໜ້າຈໍ").c_str());
  lv_obj_set_style_text_color(resetTitle, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(resetTitle, &notosanslao_shaped_25, 0);
  lv_obj_align(resetTitle, LV_ALIGN_TOP_MID, 0, 84);

  lv_obj_t* resetSub = lv_label_create(resetPopup_);
  lv_label_set_text(resetSub, "Screen Resetting...");
  lv_obj_set_style_text_color(resetSub, lv_color_hex(0x52D3A5), 0);
  lv_obj_set_style_text_font(resetSub, &lv_font_montserrat_14, 0);
  lv_obj_align(resetSub, LV_ALIGN_TOP_MID, 0, 128);

  lv_obj_add_flag(resetPopup_, LV_OBJ_FLAG_HIDDEN);

  // Remote OTA Update Popup (shown during Cloud firmware download)
  otaPopup_ = lv_obj_create(topLayer);
  lv_obj_set_size(otaPopup_, 390, 238);
  lv_obj_align(otaPopup_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(otaPopup_, lv_color_hex(0x101E2E), 0);
  lv_obj_set_style_bg_grad_color(otaPopup_, lv_color_hex(0x09131E), 0);
  lv_obj_set_style_bg_grad_dir(otaPopup_, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_color(otaPopup_, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_style_border_width(otaPopup_, 2, 0);
  lv_obj_set_style_radius(otaPopup_, 18, 0);
  lv_obj_set_style_shadow_width(otaPopup_, 40, 0);
  lv_obj_set_style_shadow_color(otaPopup_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(otaPopup_, LV_OPA_90, 0);
  lv_obj_set_style_pad_all(otaPopup_, 0, 0);
  lv_obj_clear_flag(otaPopup_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* otaSpinner = lv_spinner_create(otaPopup_, 1000, 70);
  lv_obj_set_size(otaSpinner, 56, 56);
  lv_obj_align(otaSpinner, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_arc_color(otaSpinner, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(otaSpinner, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_arc_width(otaSpinner, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(otaSpinner, 4, LV_PART_MAIN);

  lv_obj_t* otaBadge = lv_obj_create(otaPopup_);
  lv_obj_set_size(otaBadge, 40, 40);
  lv_obj_align(otaBadge, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_set_style_radius(otaBadge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(otaBadge, lv_color_hex(0x0284C7), 0);
  lv_obj_set_style_border_width(otaBadge, 0, 0);
  lv_obj_set_style_pad_all(otaBadge, 0, 0);
  lv_obj_clear_flag(otaBadge, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* otaIcon = lv_label_create(otaBadge);
  lv_label_set_text(otaIcon, LV_SYMBOL_DOWNLOAD);
  lv_obj_set_style_text_color(otaIcon, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(otaIcon, &lv_font_montserrat_20, 0);
  lv_obj_center(otaIcon);

  lv_obj_t* otaTitle = lv_label_create(otaPopup_);
  lv_label_set_text(otaTitle, shapeLaoText("ກຳລັງອັບເດດໂປຣແກຣມຜ່ານ WiFi").c_str());
  lv_obj_set_style_text_color(otaTitle, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(otaTitle, &notosanslao_shaped_20, 0);
  lv_obj_align(otaTitle, LV_ALIGN_TOP_MID, 0, 72);

  lv_obj_t* otaWarn = lv_label_create(otaPopup_);
  lv_label_set_text(otaWarn, (String(LV_SYMBOL_WARNING " ") + shapeLaoText("ກະລຸນາຢ່າປິດໄຟ (Do not turn off!)")).c_str());
  lv_obj_set_style_text_color(otaWarn, lv_color_hex(0xFBBF24), 0);
  lv_obj_set_style_text_font(otaWarn, &notosanslao_shaped_20, 0);
  lv_obj_align(otaWarn, LV_ALIGN_TOP_MID, 0, 102);

  // OTA Progress Bar
  otaBar_ = lv_bar_create(otaPopup_);
  lv_obj_set_size(otaBar_, 320, 16);
  lv_obj_align(otaBar_, LV_ALIGN_TOP_MID, 0, 138);
  lv_bar_set_range(otaBar_, 0, 100);
  lv_bar_set_value(otaBar_, 0, LV_ANIM_ON);
  lv_obj_set_style_bg_color(otaBar_, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(otaBar_, lv_color_hex(0x06B6D4), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(otaBar_, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(otaBar_, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_radius(otaBar_, 8, LV_PART_MAIN);
  lv_obj_set_style_radius(otaBar_, 8, LV_PART_INDICATOR);

  otaPercentLabel_ = lv_label_create(otaPopup_);
  lv_label_set_text(otaPercentLabel_, "0 %");
  lv_obj_set_style_text_color(otaPercentLabel_, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_style_text_font(otaPercentLabel_, &lv_font_montserrat_20, 0);
  lv_obj_align(otaPercentLabel_, LV_ALIGN_TOP_MID, 0, 160);

  otaMsgLabel_ = lv_label_create(otaPopup_);
  lv_label_set_text(otaMsgLabel_, shapeLaoText("ກຳລັງເຊື່ອມຕໍ່ລະບົບ...").c_str());
  lv_obj_set_style_text_color(otaMsgLabel_, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(otaMsgLabel_, &notosanslao_shaped_20, 0);
  lv_obj_align(otaMsgLabel_, LV_ALIGN_TOP_MID, 0, 190);

  lv_obj_add_flag(otaPopup_, LV_OBJ_FLAG_HIDDEN);

  settingsButton_.parent(home_);
  settingsButton_.size(44, 44);
  settingsButton_.align(Align::BottomRight, -10, -10);
  settingsButton_.color(0x263746);
  settingsButton_.radius(6);
  settingsButton_.onClicked(settingsClicked, this);
  settingsButton_.hidden(true);

  configurePage(select_, 0xB8DCFA); // Rich sky-blue top gradient base
  lv_obj_set_style_bg_grad_color(select_.native(), lv_color_hex(0xE2F1FD), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(select_.native(), LV_GRAD_DIR_VER, LV_PART_MAIN);

  selectTitle_.parent(select_);
  selectTitle_.font(&notosanslao_shaped_20); // Reduced Lao title size to 20px
  selectTitle_.textColor(0x0B2239); // Deep crisp navy
  selectTitle_.align(Align::TopLeft, 130, 8); // Positioned at y=8 to align with back button
  lv_obj_set_width(selectTitle_.native(), 340); // Width spans the gap between back button and right edge (130 to 470)
  lv_obj_set_style_text_align(selectTitle_.native(), LV_TEXT_ALIGN_CENTER, 0);

  // Divider line (aligned with bottom edge of back button at y=58)
  selectDivider_ = lv_obj_create(select_.native());
  lv_obj_set_size(selectDivider_, 340, 2);
  lv_obj_set_pos(selectDivider_, 130, 58);
  lv_obj_set_style_bg_color(selectDivider_, lv_color_hex(0x5CA2DC), 0);
  lv_obj_set_style_border_width(selectDivider_, 0, 0);
  lv_obj_clear_flag(selectDivider_, LV_OBJ_FLAG_SCROLLABLE);

  // Subtitle (centered below the title)
  selectSubTitle_.parent(select_);
  selectSubTitle_.font(&lv_font_montserrat_14); // Reduced to Montserrat 14 for better fit
  selectSubTitle_.textColor(0x2B4C6B);
  selectSubTitle_.text("Choose payment by temperature");
  lv_obj_align_to(selectSubTitle_.native(), selectTitle_.native(), LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

  // Redesigned select back button (centered back arrow and BACK text)
  selectBack_.parent(select_);
  selectBack_.size(116, 50);
  selectBack_.align(Align::TopLeft, 10, 8);
  selectBack_.color(0x0A5C9E);
  selectBack_.radius(8);
  selectBack_.text(LV_SYMBOL_LEFT "  BACK");
  selectBack_.label().font(&lv_font_montserrat_20);
  selectBack_.onClicked(backClicked, this);

  // Swapped positions: HOT (left), WARM (middle), COLD (right)
  const char* laoNames[3] = {"ນ້ຳເຢັນ", "ນ້ຳອຸ່ນ", "ນ້ຳຮ້ອນ"};
  const char* engNames[3] = {"COLD", "WARM", "HOT"};
  const uint32_t colors[3] = {0x1A8CFF, 0xFEBD3B, 0xFF5522}; // Cold, Warm, Hot colors
  const uint32_t pillColors[3] = {0x0062CC, 0xDF8800, 0xD93400}; // Crisp capsule badge colors
  const uint32_t cardAccentColors[3] = {0x1A8CFF, 0xF09B0A, 0xFF5522}; // Matching vivid accent for price cards

  for (uint8_t opt = 1; opt <= 3; ++opt) {
    uint8_t i = opt - 1; // 0: Cold, 1: Warm, 2: Hot
    uint8_t col = 3 - opt; // 2: Cold (right), 1: Warm (middle), 0: Hot (left)
    int x_pos = 10 + col * 155;

    optionButtons_[i].parent(select_);
    optionButtons_[i].text(laoNames[i]);
    optionButtons_[i].label().font(&notosanslao_shaped_25);
    optionButtons_[i].size(142, 166);
    optionButtons_[i].position(x_pos, 70);
    optionButtons_[i].color(colors[i]);
    optionButtons_[i].radius(12);
    optionButtons_[i].onClicked(optionClicked, reinterpret_cast<void*>(opt));
    lv_obj_set_style_text_align(optionButtons_[i].label().native(), LV_TEXT_ALIGN_CENTER, 0);

    // 3D tactile dimensions: Drop Shadow and semi-transparent white highlight border
    lv_obj_t* nativeBtn = optionButtons_[i].native();
    lv_obj_set_style_pad_all(nativeBtn, 0, 0);
    lv_obj_set_style_shadow_width(nativeBtn, 14, 0);
    lv_obj_set_style_shadow_opa(nativeBtn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(nativeBtn, lv_color_hex(0x173E59), 0);
    lv_obj_set_style_shadow_ofs_y(nativeBtn, 5, 0);
    lv_obj_set_style_border_width(nativeBtn, 2, 0);
    lv_obj_set_style_border_color(nativeBtn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(nativeBtn, LV_OPA_40, 0);
    lv_obj_set_style_translate_y(nativeBtn, 2, LV_STATE_PRESSED);

    // Smart Home Water Wave Card: Double layered waves at bottom + clean Lao text & capsule pill at top
    createTempBadgeIcon(nativeBtn, opt);
    lv_obj_t* btnLbl = optionButtons_[i].label().native();
    lv_obj_move_foreground(btnLbl);
    lv_obj_align(btnLbl, LV_ALIGN_TOP_MID, 0, 12);

    // Sleek capsule badge [ HOT ] / [ WARM ] / [ COLD ] below Lao text, above the water waves
    lv_obj_t* engPill = lv_obj_create(nativeBtn);
    lv_obj_set_size(engPill, 94, 28);
    lv_obj_align(engPill, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_radius(engPill, 14, 0);
    lv_obj_set_style_bg_color(engPill, lv_color_hex(pillColors[i]), 0);
    lv_obj_set_style_bg_opa(engPill, LV_OPA_80, 0);
    lv_obj_set_style_border_width(engPill, 1, 0);
    lv_obj_set_style_border_color(engPill, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(engPill, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(engPill, 0, 0);
    lv_obj_clear_flag(engPill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(engPill, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* engLbl = lv_label_create(engPill);
    lv_label_set_text(engLbl, engNames[i]);
    lv_obj_set_style_text_font(engLbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(engLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(engLbl);

    priceCards_[i] = lv_obj_create(select_.native());
    lv_obj_set_size(priceCards_[i], 142, 56);
    lv_obj_set_pos(priceCards_[i], x_pos, 248);
    lv_obj_set_style_radius(priceCards_[i], 10, 0);
    lv_obj_set_style_bg_color(priceCards_[i], lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(priceCards_[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(priceCards_[i], 2, 0);
    lv_obj_set_style_border_color(priceCards_[i], lv_color_hex(cardAccentColors[i]), 0);
    lv_obj_set_style_shadow_width(priceCards_[i], 14, 0);
    lv_obj_set_style_shadow_opa(priceCards_[i], LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(priceCards_[i], lv_color_hex(0x173E59), 0);
    lv_obj_set_style_shadow_ofs_y(priceCards_[i], 4, 0);
    lv_obj_set_style_pad_all(priceCards_[i], 0, 0);
    lv_obj_clear_flag(priceCards_[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(priceCards_[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(priceCards_[i], priceCardClicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(opt)));

    // Colored bottom ribbon inside price card matching the temperature button
    lv_obj_t* priceRibbon = lv_obj_create(priceCards_[i]);
    lv_obj_set_size(priceRibbon, 138, 19);
    lv_obj_align(priceRibbon, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(priceRibbon, 6, 0);
    lv_obj_set_style_bg_color(priceRibbon, lv_color_hex(cardAccentColors[i]), 0);
    lv_obj_set_style_bg_opa(priceRibbon, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(priceRibbon, 0, 0);
    lv_obj_set_style_pad_all(priceRibbon, 0, 0);
    lv_obj_clear_flag(priceRibbon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(priceRibbon, LV_OBJ_FLAG_CLICKABLE);

    // Static sub-label inside colored ribbon
    lv_obj_t* priceTitle = lv_label_create(priceRibbon);
    lv_label_set_text(priceTitle, "PRICE");
    lv_obj_set_style_text_font(priceTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(priceTitle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(priceTitle);

    priceLabels_[i].parent(select_);
    lv_obj_set_parent(priceLabels_[i].native(), priceCards_[i]);
    priceLabels_[i].font(&lv_font_montserrat_20);
    priceLabels_[i].text("-- LAK");
    priceLabels_[i].textColor(0x0B2239);
    priceLabels_[i].align(Align::TopCenter, 0, 6);
  }

  configurePage(qr_, 0xB8DCFA); // Rich sky-blue top gradient base matching theme
  lv_obj_set_style_bg_grad_color(qr_.native(), lv_color_hex(0xE2F1FD), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(qr_.native(), LV_GRAD_DIR_VER, LV_PART_MAIN);

  qrTitle_.parent(qr_);
  qrTitle_.text("ສະແກນຈ່າຍເງິນດ້ວຍແອບທະນາຄານ");
  qrTitle_.font(&notosanslao_shaped_20); // Reduced Lao title size to 20px
  qrTitle_.textColor(0x0B2239); // Deep crisp navy
  qrTitle_.align(Align::TopLeft, 130, 8); // Positioned at y=8 to align with back button
  lv_obj_set_width(qrTitle_.native(), 340); // Width spans the gap between back button and right edge (130 to 470)
  lv_obj_set_style_text_align(qrTitle_.native(), LV_TEXT_ALIGN_CENTER, 0);

  // Divider line (aligned with bottom edge of back button at y=58)
  qrDivider_ = lv_obj_create(qr_.native());
  lv_obj_set_size(qrDivider_, 340, 2);
  lv_obj_set_pos(qrDivider_, 130, 58);
  lv_obj_set_style_bg_color(qrDivider_, lv_color_hex(0x5CA2DC), 0);
  lv_obj_set_style_border_width(qrDivider_, 0, 0);
  lv_obj_clear_flag(qrDivider_, LV_OBJ_FLAG_SCROLLABLE);

  // Subtitle (centered below the title)
  qrSubTitle_.parent(qr_);
  qrSubTitle_.text("Scan to pay with Mobile Banking");
  qrSubTitle_.font(&lv_font_montserrat_14); // Reduced to Montserrat 14 for better fit
  qrSubTitle_.textColor(0x2B4C6B);
  lv_obj_align_to(qrSubTitle_.native(), qrTitle_.native(), LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

  // Redesigned QR back button (centered back arrow and BACK text)
  qrBack_.parent(qr_);
  qrBack_.size(116, 50);
  qrBack_.align(Align::TopLeft, 10, 8);
  qrBack_.color(0x0A5C9E);
  qrBack_.radius(8);
  qrBack_.text(LV_SYMBOL_LEFT "  BACK");
  qrBack_.label().font(&lv_font_montserrat_20);
  qrBack_.onClicked(backClicked, this);

  // Centered QR Card (210x210, centered vertically between y=60 divider and y=280 bottom bar -> y=65)
  qrCard_ = lv_obj_create(qr_.native());
  lv_obj_set_size(qrCard_, 210, 210);
  lv_obj_set_pos(qrCard_, 135, 65);
  lv_obj_set_style_radius(qrCard_, 14, 0);
  lv_obj_set_style_bg_color(qrCard_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(qrCard_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(qrCard_, 2, 0);
  lv_obj_set_style_border_color(qrCard_, lv_color_hex(0x1A8CFF), 0);
  lv_obj_set_style_shadow_width(qrCard_, 18, 0);
  lv_obj_set_style_shadow_opa(qrCard_, LV_OPA_30, 0);
  lv_obj_set_style_shadow_color(qrCard_, lv_color_hex(0x173E59), 0);
  lv_obj_set_style_shadow_ofs_y(qrCard_, 5, 0);
  lv_obj_set_style_pad_all(qrCard_, 0, 0);
  lv_obj_clear_flag(qrCard_, LV_OBJ_FLAG_SCROLLABLE);

  // QR Code (190x190) centered equally on all 4 sides inside the 210x210 card
  qrCode_ = lv_qrcode_create(qrCard_, 190, lv_color_hex(0x111111), lv_color_hex(0xFFFFFF));
  lv_obj_center(qrCode_);
  const char* waiting = "WAITING_FOR_PAYMENT";
  lv_qrcode_update(qrCode_, waiting, strlen(waiting));

  // Keep LaoQR logo centered inside the QR Code
  laoQr_.parent(qr_);
  lv_obj_set_parent(laoQr_.native(), qrCard_);
  lv_obj_center(laoQr_.native());
  lv_obj_add_flag(qrCard_, LV_OBJ_FLAG_HIDDEN); // Hidden by default (until loaded)

  // Centered Loading Box (210x210, centered vertically between y=60 divider and y=280 bottom bar -> y=65)
  qrLoadingBox_ = lv_obj_create(qr_.native());
  lv_obj_set_size(qrLoadingBox_, 210, 210);
  lv_obj_set_pos(qrLoadingBox_, 135, 65);
  lv_obj_set_style_radius(qrLoadingBox_, 14, 0);
  lv_obj_set_style_bg_color(qrLoadingBox_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(qrLoadingBox_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(qrLoadingBox_, 2, 0);
  lv_obj_set_style_border_color(qrLoadingBox_, lv_color_hex(0x1A8CFF), 0);
  lv_obj_set_style_shadow_width(qrLoadingBox_, 18, 0);
  lv_obj_set_style_shadow_opa(qrLoadingBox_, LV_OPA_30, 0);
  lv_obj_set_style_shadow_color(qrLoadingBox_, lv_color_hex(0x173E59), 0);
  lv_obj_set_style_shadow_ofs_y(qrLoadingBox_, 5, 0);
  lv_obj_set_style_pad_all(qrLoadingBox_, 0, 0);
  lv_obj_set_style_clip_corner(qrLoadingBox_, true, 0);
  lv_obj_clear_flag(qrLoadingBox_, LV_OBJ_FLAG_SCROLLABLE);

  // Pulsing outer halo behind the dual spinner rings
  qrLoadingHalo_ = lv_obj_create(qrLoadingBox_);
  lv_obj_set_size(qrLoadingHalo_, 126, 126);
  lv_obj_align(qrLoadingHalo_, LV_ALIGN_CENTER, 0, -16);
  lv_obj_set_style_radius(qrLoadingHalo_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(qrLoadingHalo_, lv_color_hex(0xE0F2FE), 0);
  lv_obj_set_style_bg_opa(qrLoadingHalo_, LV_OPA_80, 0);
  lv_obj_set_style_border_width(qrLoadingHalo_, 0, 0);
  lv_obj_clear_flag(qrLoadingHalo_, LV_OBJ_FLAG_SCROLLABLE);

  // Outer sweeping spinner ring
  qrLoadingArc_ = lv_arc_create(qrLoadingBox_);
  lv_obj_set_size(qrLoadingArc_, 116, 116);
  lv_obj_align(qrLoadingArc_, LV_ALIGN_CENTER, 0, -16);
  lv_arc_set_bg_angles(qrLoadingArc_, 0, 360);
  lv_arc_set_angles(qrLoadingArc_, 0, 210);
  lv_obj_set_style_arc_width(qrLoadingArc_, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_color(qrLoadingArc_, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
  lv_obj_set_style_arc_width(qrLoadingArc_, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(qrLoadingArc_, lv_color_hex(0x1A8CFF), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(qrLoadingArc_, true, LV_PART_INDICATOR);
  lv_obj_remove_style(qrLoadingArc_, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(qrLoadingArc_, LV_OBJ_FLAG_CLICKABLE);

  // Inner counter-rotating ring
  qrLoadingInnerArc_ = lv_arc_create(qrLoadingBox_);
  lv_obj_set_size(qrLoadingInnerArc_, 90, 90);
  lv_obj_align(qrLoadingInnerArc_, LV_ALIGN_CENTER, 0, -16);
  lv_arc_set_bg_angles(qrLoadingInnerArc_, 0, 360);
  lv_arc_set_angles(qrLoadingInnerArc_, 0, 120);
  lv_obj_set_style_arc_width(qrLoadingInnerArc_, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_color(qrLoadingInnerArc_, lv_color_hex(0xF1F5F9), LV_PART_MAIN);
  lv_obj_set_style_arc_width(qrLoadingInnerArc_, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(qrLoadingInnerArc_, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(qrLoadingInnerArc_, true, LV_PART_INDICATOR);
  lv_obj_remove_style(qrLoadingInnerArc_, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(qrLoadingInnerArc_, LV_OBJ_FLAG_CLICKABLE);

  // Center Mini QR Scanner Viewfinder (60x60 rounded box with 2x2 QR finder blocks + sweeping laser)
  qrScannerBox_ = lv_obj_create(qrLoadingBox_);
  lv_obj_set_size(qrScannerBox_, 60, 60);
  lv_obj_align(qrScannerBox_, LV_ALIGN_CENTER, 0, -16);
  lv_obj_set_style_radius(qrScannerBox_, 12, 0);
  lv_obj_set_style_bg_color(qrScannerBox_, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_bg_opa(qrScannerBox_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(qrScannerBox_, 2, 0);
  lv_obj_set_style_border_color(qrScannerBox_, lv_color_hex(0x1A8CFF), 0);
  lv_obj_set_style_pad_all(qrScannerBox_, 0, 0);
  lv_obj_clear_flag(qrScannerBox_, LV_OBJ_FLAG_SCROLLABLE);

  const int qx[4] = {9, 31, 9, 31};
  const int qy[4] = {9, 9, 31, 31};
  for (uint8_t b = 0; b < 4; ++b) {
    qrFinderBlocks_[b] = lv_obj_create(qrScannerBox_);
    lv_obj_set_size(qrFinderBlocks_[b], 16, 16);
    lv_obj_set_pos(qrFinderBlocks_[b], qx[b], qy[b]);
    lv_obj_set_style_radius(qrFinderBlocks_[b], 4, 0);
    lv_obj_set_style_pad_all(qrFinderBlocks_[b], 0, 0);
    lv_obj_clear_flag(qrFinderBlocks_[b], LV_OBJ_FLAG_SCROLLABLE);
    if (b < 3) {
      // 3 QR corner finder squares (hollow white frame)
      lv_obj_set_style_bg_color(qrFinderBlocks_[b], lv_color_hex(0x0F172A), 0);
      lv_obj_set_style_border_width(qrFinderBlocks_[b], 3, 0);
      lv_obj_set_style_border_color(qrFinderBlocks_[b], lv_color_hex(0xFFFFFF), 0);
    } else {
      // Bottom-right themed data module
      lv_obj_set_style_bg_color(qrFinderBlocks_[b], lv_color_hex(0x1A8CFF), 0);
      lv_obj_set_style_border_width(qrFinderBlocks_[b], 0, 0);
    }
  }

  // Sweeping horizontal laser scan bar across the mini QR viewfinder
  qrScanLaser_ = lv_obj_create(qrScannerBox_);
  lv_obj_set_size(qrScanLaser_, 48, 4);
  lv_obj_set_pos(qrScanLaser_, 4, 8);
  lv_obj_set_style_radius(qrScanLaser_, 2, 0);
  lv_obj_set_style_bg_color(qrScanLaser_, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_style_border_width(qrScanLaser_, 0, 0);
  lv_obj_set_style_shadow_width(qrScanLaser_, 10, 0);
  lv_obj_set_style_shadow_color(qrScanLaser_, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_style_shadow_opa(qrScanLaser_, LV_OPA_90, 0);
  lv_obj_clear_flag(qrScanLaser_, LV_OBJ_FLAG_SCROLLABLE);

  // 4 rising water/theme bubbles on the left & right sides of the loading card
  for (uint8_t b = 0; b < 4; ++b) {
    qrLoadingBubbles_[b] = lv_obj_create(qrLoadingBox_);
    const int bSz = (b % 2 == 0) ? 10 : 7;
    lv_obj_set_size(qrLoadingBubbles_[b], bSz, bSz);
    lv_obj_set_style_radius(qrLoadingBubbles_[b], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(qrLoadingBubbles_[b], lv_color_hex(0x1A8CFF), 0);
    lv_obj_set_style_bg_opa(qrLoadingBubbles_[b], LV_OPA_60, 0);
    lv_obj_set_style_border_width(qrLoadingBubbles_[b], 1, 0);
    lv_obj_set_style_border_color(qrLoadingBubbles_[b], lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(qrLoadingBubbles_[b], LV_OBJ_FLAG_SCROLLABLE);
  }

  // Loading label ("ກຳລັງໂຫຼດ...")
  qrLoadingLabel_.parent(qr_);
  lv_obj_set_parent(qrLoadingLabel_.native(), qrLoadingBox_);
  qrLoadingLabel_.font(&notosanslao_shaped_20);
  qrLoadingLabel_.textColor(0x0B2239);
  qrLoadingLabel_.text("ກຳລັງໂຫຼດ");
  qrLoadingLabel_.align(Align::BottomCenter, 0, -10);
  lv_obj_set_style_text_align(qrLoadingLabel_.native(), LV_TEXT_ALIGN_CENTER, 0);

  // Right-side QR Page Countdown Timer Card (centered in the right space x=356..468)
  qrTimeoutCard_ = lv_obj_create(qr_.native());
  lv_obj_set_size(qrTimeoutCard_, 112, 134);
  lv_obj_set_pos(qrTimeoutCard_, 356, 103);
  lv_obj_set_style_radius(qrTimeoutCard_, 14, 0);
  lv_obj_set_style_bg_color(qrTimeoutCard_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(qrTimeoutCard_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(qrTimeoutCard_, 2, 0);
  lv_obj_set_style_border_color(qrTimeoutCard_, lv_color_hex(0x1A8CFF), 0);
  lv_obj_set_style_shadow_width(qrTimeoutCard_, 14, 0);
  lv_obj_set_style_shadow_opa(qrTimeoutCard_, LV_OPA_20, 0);
  lv_obj_set_style_shadow_color(qrTimeoutCard_, lv_color_hex(0x173E59), 0);
  lv_obj_set_style_shadow_ofs_y(qrTimeoutCard_, 4, 0);
  lv_obj_set_style_pad_all(qrTimeoutCard_, 0, 0);
  lv_obj_clear_flag(qrTimeoutCard_, LV_OBJ_FLAG_SCROLLABLE);

  qrTimeoutArc_ = lv_arc_create(qrTimeoutCard_);
  lv_obj_set_size(qrTimeoutArc_, 76, 76);
  lv_obj_align(qrTimeoutArc_, LV_ALIGN_TOP_MID, 0, 12);
  lv_arc_set_rotation(qrTimeoutArc_, 270);
  lv_arc_set_bg_angles(qrTimeoutArc_, 0, 360);
  lv_arc_set_range(qrTimeoutArc_, 0, config::QrScanTimeoutMs / 1000U);
  lv_arc_set_value(qrTimeoutArc_, config::QrScanTimeoutMs / 1000U);
  lv_obj_set_style_arc_width(qrTimeoutArc_, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(qrTimeoutArc_, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
  lv_obj_set_style_arc_width(qrTimeoutArc_, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(qrTimeoutArc_, lv_color_hex(0x1A8CFF), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(qrTimeoutArc_, true, LV_PART_INDICATOR);
  lv_obj_remove_style(qrTimeoutArc_, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(qrTimeoutArc_, LV_OBJ_FLAG_CLICKABLE);

  qrTimeoutValLabel_ = lv_label_create(qrTimeoutArc_);
  lv_label_set_text_fmt(qrTimeoutValLabel_, "%us", static_cast<unsigned>(config::QrScanTimeoutMs / 1000U));
  lv_obj_set_style_text_font(qrTimeoutValLabel_, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(qrTimeoutValLabel_, lv_color_hex(0x0B2239), 0);
  lv_obj_center(qrTimeoutValLabel_);

  lv_obj_t* qrTimeTitle = lv_label_create(qrTimeoutCard_);
  lv_label_set_text(qrTimeTitle, shapeLaoText("ເວລາສະແກນ").c_str());
  lv_obj_set_style_text_font(qrTimeTitle, &notosanslao_shaped_20, 0);
  lv_obj_set_style_text_color(qrTimeTitle, lv_color_hex(0x2B4C6B), 0);
  lv_obj_align(qrTimeTitle, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_add_flag(qrTimeoutCard_, LV_OBJ_FLAG_HIDDEN);

  // Bottom info bar (x=0, y=280, width=480, height=40, touching bottom edge)
  qrBottomBar_ = lv_obj_create(qr_.native());
  lv_obj_set_size(qrBottomBar_, 480, 40);
  lv_obj_set_pos(qrBottomBar_, 0, 280);
  lv_obj_set_style_radius(qrBottomBar_, 0, 0);
  lv_obj_set_style_bg_opa(qrBottomBar_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(qrBottomBar_, 0, 0);
  lv_obj_set_style_pad_all(qrBottomBar_, 0, 0);
  lv_obj_clear_flag(qrBottomBar_, LV_OBJ_FLAG_SCROLLABLE);

  qrInfoLabel_.parent(qr_);
  lv_obj_set_parent(qrInfoLabel_.native(), qrBottomBar_);
  qrInfoLabel_.font(&notosanslao_shaped_20);
  qrInfoLabel_.textColor(0xFFFFFF); // White text inside colored bottom bar
  qrInfoLabel_.align(Align::Center, 0, 0);
  lv_obj_set_style_text_align(qrInfoLabel_.native(), LV_TEXT_ALIGN_CENTER, 0);

  configurePage(payment_, 0xB8DCFA); // Rich sky-blue top gradient base
  lv_obj_set_style_bg_grad_color(payment_.native(), lv_color_hex(0xE2F1FD), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(payment_.native(), LV_GRAD_DIR_VER, LV_PART_MAIN);
  
  // Centered modal card for Payment (356x252 with golden top accent)
  paymentCard_ = lv_obj_create(payment_.native());
  lv_obj_set_size(paymentCard_, 356, 252);
  lv_obj_align(paymentCard_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(paymentCard_, 16, 0);
  lv_obj_set_style_bg_color(paymentCard_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(paymentCard_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(paymentCard_, 2, 0);
  lv_obj_set_style_border_color(paymentCard_, lv_color_hex(0xF59E0B), 0);
  lv_obj_set_style_shadow_width(paymentCard_, 22, 0);
  lv_obj_set_style_shadow_opa(paymentCard_, LV_OPA_30, 0);
  lv_obj_set_style_shadow_color(paymentCard_, lv_color_hex(0x173E59), 0);
  lv_obj_set_style_shadow_ofs_y(paymentCard_, 6, 0);
  lv_obj_set_style_pad_all(paymentCard_, 0, 0);
  lv_obj_clear_flag(paymentCard_, LV_OBJ_FLAG_SCROLLABLE);

  // Pulsing golden halo behind the washer machine
  paymentHalo_ = lv_obj_create(paymentCard_);
  lv_obj_set_size(paymentHalo_, 112, 112);
  lv_obj_align(paymentHalo_, LV_ALIGN_TOP_MID, 0, 26);
  lv_obj_set_style_radius(paymentHalo_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(paymentHalo_, lv_color_hex(0xFEF3C7), 0);
  lv_obj_set_style_bg_opa(paymentHalo_, LV_OPA_70, 0);
  lv_obj_set_style_border_width(paymentHalo_, 2, 0);
  lv_obj_set_style_border_color(paymentHalo_, lv_color_hex(0xFDE68A), 0);
  lv_obj_clear_flag(paymentHalo_, LV_OBJ_FLAG_SCROLLABLE);

  // Positioned machine centered at y=16
  paymentMachine_ = createWasherMachine(paymentCard_, 0, 16, 140, 122, 0xF8FBFD, &paymentDrum_);
  lv_obj_align(paymentMachine_, LV_ALIGN_TOP_MID, 0, 16);
  lv_obj_set_size(paymentDrum_, 68, 68);
  lv_obj_align(paymentDrum_, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_obj_set_style_arc_width(paymentDrum_, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(paymentDrum_, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(paymentDrum_, lv_color_hex(0xF59E0B), LV_PART_INDICATOR);
  
  // Style the inside of the drum to look like a rich gold coin badge
  lv_obj_set_style_bg_opa(paymentDrum_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(paymentDrum_, lv_color_hex(0xFFE59E), 0);
  lv_obj_set_style_radius(paymentDrum_, LV_RADIUS_CIRCLE, 0);

  lv_obj_t* dollarLabel = lv_label_create(paymentDrum_);
  lv_label_set_text(dollarLabel, "$");
  lv_obj_set_style_text_font(dollarLabel, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(dollarLabel, lv_color_hex(0x996B00), 0);
  lv_obj_center(dollarLabel);
  
  // 3 Animated Flying Gold Coins that stream into the washer drum
  for (uint8_t i = 0; i < 3; ++i) {
    creditDots_[i] = createDot(paymentCard_, 24, 0xFFD34E);
    lv_obj_set_style_border_color(creditDots_[i], lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_width(creditDots_[i], 2, 0);
    lv_obj_t* coinSym = lv_label_create(creditDots_[i]);
    lv_label_set_text(coinSym, "$");
    lv_obj_set_style_text_font(coinSym, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(coinSym, lv_color_hex(0x996B00), 0);
    lv_obj_center(coinSym);
  }

  paymentText_.parent(payment_);
  lv_obj_set_parent(paymentText_.native(), paymentCard_);
  paymentText_.font(&notosanslao_shaped_20);
  paymentText_.textColor(0x0B2239);
  paymentText_.size(336, 52);
  paymentText_.align(Align::BottomCenter, 0, -28);
  lv_obj_set_style_text_align(paymentText_.native(), LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(paymentText_.native(), 2, 0);

  lv_obj_t* paySub = lv_label_create(paymentCard_);
  lv_label_set_text(paySub, "TRANSFERRING CREDIT...");
  lv_obj_set_style_text_font(paySub, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(paySub, lv_color_hex(0xD97706), 0);
  lv_obj_align(paySub, LV_ALIGN_BOTTOM_MID, 0, -8);

  configurePage(paymentSuccess_, 0xB8DCFA);
  lv_obj_set_style_bg_grad_color(paymentSuccess_.native(), lv_color_hex(0xE2F1FD), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(paymentSuccess_.native(), LV_GRAD_DIR_VER, LV_PART_MAIN);

  // Centered modal card for PaymentSuccess (356x252 with emerald accent)
  paymentSuccessCard_ = lv_obj_create(paymentSuccess_.native());
  lv_obj_set_size(paymentSuccessCard_, 356, 252);
  lv_obj_align(paymentSuccessCard_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(paymentSuccessCard_, 16, 0);
  lv_obj_set_style_bg_color(paymentSuccessCard_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(paymentSuccessCard_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(paymentSuccessCard_, 2, 0);
  lv_obj_set_style_border_color(paymentSuccessCard_, lv_color_hex(0x12B76A), 0);
  lv_obj_set_style_shadow_width(paymentSuccessCard_, 22, 0);
  lv_obj_set_style_shadow_opa(paymentSuccessCard_, LV_OPA_30, 0);
  lv_obj_set_style_shadow_color(paymentSuccessCard_, lv_color_hex(0x173E59), 0);
  lv_obj_set_style_shadow_ofs_y(paymentSuccessCard_, 6, 0);
  lv_obj_set_style_pad_all(paymentSuccessCard_, 0, 0);
  lv_obj_clear_flag(paymentSuccessCard_, LV_OBJ_FLAG_SCROLLABLE);

  // Layer 1: Expanding Ripple Halo behind the emerald checkmark badge
  paymentSuccessHalo_ = lv_obj_create(paymentSuccessCard_);
  lv_obj_set_size(paymentSuccessHalo_, 136, 136);
  lv_obj_set_pos(paymentSuccessHalo_, 178 - 68, 88 - 68);
  lv_obj_set_style_radius(paymentSuccessHalo_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(paymentSuccessHalo_, lv_color_hex(0xD1FAE5), 0);
  lv_obj_set_style_bg_opa(paymentSuccessHalo_, LV_OPA_50, 0);
  lv_obj_set_style_border_width(paymentSuccessHalo_, 3, 0);
  lv_obj_set_style_border_color(paymentSuccessHalo_, lv_color_hex(0x34D399), 0);
  lv_obj_set_style_border_opa(paymentSuccessHalo_, LV_OPA_70, 0);
  lv_obj_clear_flag(paymentSuccessHalo_, LV_OBJ_FLAG_SCROLLABLE);

  // Layer 2: Sweeping Outer Emerald Ring (lv_arc 0..360)
  paymentSuccessArc_ = lv_arc_create(paymentSuccessCard_);
  lv_obj_set_size(paymentSuccessArc_, 122, 122);
  lv_obj_set_pos(paymentSuccessArc_, 178 - 61, 88 - 61);
  lv_arc_set_rotation(paymentSuccessArc_, 270);
  lv_arc_set_bg_angles(paymentSuccessArc_, 0, 360);
  lv_arc_set_range(paymentSuccessArc_, 0, 360);
  lv_arc_set_value(paymentSuccessArc_, 360);
  lv_obj_set_style_arc_width(paymentSuccessArc_, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(paymentSuccessArc_, lv_color_hex(0xE6F9F0), LV_PART_MAIN);
  lv_obj_set_style_arc_width(paymentSuccessArc_, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(paymentSuccessArc_, lv_color_hex(0x059669), LV_PART_INDICATOR);
  lv_obj_remove_style(paymentSuccessArc_, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(paymentSuccessArc_, LV_OBJ_FLAG_CLICKABLE);

  // Layer 3: Central Bouncing Emerald Badge
  paymentSuccessCircle_ = lv_obj_create(paymentSuccessCard_);
  lv_obj_set_size(paymentSuccessCircle_, 98, 98);
  lv_obj_set_pos(paymentSuccessCircle_, 178 - 49, 88 - 49);
  lv_obj_set_style_radius(paymentSuccessCircle_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(paymentSuccessCircle_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(paymentSuccessCircle_, lv_color_hex(0x12B76A), 0);
  lv_obj_set_style_bg_grad_color(paymentSuccessCircle_, lv_color_hex(0x059669), 0);
  lv_obj_set_style_bg_grad_dir(paymentSuccessCircle_, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(paymentSuccessCircle_, 3, 0);
  lv_obj_set_style_border_color(paymentSuccessCircle_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_shadow_width(paymentSuccessCircle_, 14, 0);
  lv_obj_set_style_shadow_color(paymentSuccessCircle_, lv_color_hex(0x12B76A), 0);
  lv_obj_set_style_shadow_opa(paymentSuccessCircle_, LV_OPA_40, 0);
  lv_obj_set_style_pad_all(paymentSuccessCircle_, 0, 0);
  lv_obj_clear_flag(paymentSuccessCircle_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(paymentSuccessCircle_, LV_OBJ_FLAG_CLICKABLE);

  // Layer 4: Large Crisp Checkmark (Montserrat 48)
  paymentSuccessCheck_ = lv_label_create(paymentSuccessCircle_);
  lv_label_set_text(paymentSuccessCheck_, LV_SYMBOL_OK);
  lv_obj_set_style_text_font(paymentSuccessCheck_, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(paymentSuccessCheck_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(paymentSuccessCheck_, LV_ALIGN_CENTER, 0, 0);

  // Layer 5: 8 Confetti / Sparkle Burst Particles around the circle
  const uint32_t sparkColors[8] = {
    0xFFD34E, 0x12B76A, 0x38BDF8, 0xFF7A45,
    0xFBBF24, 0x10B981, 0x60A5FA, 0xF43F5E
  };
  for (uint8_t i = 0; i < 8; ++i) {
    paymentSuccessSparks_[i] = createDot(paymentSuccessCard_, (i % 2 == 0) ? 12 : 9, sparkColors[i]);
    lv_obj_set_style_border_width(paymentSuccessSparks_[i], 1, 0);
  }

  paymentSuccessText_.parent(paymentSuccess_);
  lv_obj_set_parent(paymentSuccessText_.native(), paymentSuccessCard_);
  paymentSuccessText_.font(&notosanslao_shaped_20);
  paymentSuccessText_.textColor(0x0B2239);
  paymentSuccessText_.size(336, 34);
  paymentSuccessText_.align(Align::BottomCenter, 0, -30);
  lv_obj_set_style_text_align(paymentSuccessText_.native(), LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t* succSub = lv_label_create(paymentSuccessCard_);
  lv_label_set_text(succSub, "PAYMENT SUCCESSFUL");
  lv_obj_set_style_text_font(succSub, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(succSub, lv_color_hex(0x059669), 0);
  lv_obj_align(succSub, LV_ALIGN_BOTTOM_MID, 0, -10);

  // ==================== Industrial Front-Load Washer Dashboard (Page::Running) ====================
  configurePage(running_, 0x0B1522);
  lv_obj_set_style_bg_grad_color(running_.native(), lv_color_hex(0x162940), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(running_.native(), LV_GRAD_DIR_VER, LV_PART_MAIN);

  // LEFT SIDE: Realistic Industrial Stainless-Steel Front-Load Washer (206x292 at x=12, y=14)
  runningMachine_ = lv_obj_create(running_.native());
  lv_obj_set_size(runningMachine_, 206, 292);
  lv_obj_set_pos(runningMachine_, 12, 14);
  lv_obj_set_style_radius(runningMachine_, 14, 0);
  lv_obj_set_style_bg_color(runningMachine_, lv_color_hex(0xDCE4EC), 0);
  lv_obj_set_style_bg_grad_color(runningMachine_, lv_color_hex(0x9BA9B8), 0);
  lv_obj_set_style_bg_grad_dir(runningMachine_, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(runningMachine_, 3, 0);
  lv_obj_set_style_border_color(runningMachine_, lv_color_hex(0xF8FAFC), 0);
  lv_obj_set_style_shadow_width(runningMachine_, 24, 0);
  lv_obj_set_style_shadow_opa(runningMachine_, LV_OPA_60, 0);
  lv_obj_set_style_shadow_color(runningMachine_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_ofs_y(runningMachine_, 6, 0);
  lv_obj_set_style_pad_all(runningMachine_, 0, 0);
  lv_obj_clear_flag(runningMachine_, LV_OBJ_FLAG_SCROLLABLE);

  // Industrial Top Control Header Panel
  lv_obj_t* indPanel = lv_obj_create(runningMachine_);
  lv_obj_set_size(indPanel, 190, 44);
  lv_obj_set_pos(indPanel, 5, 6);
  lv_obj_set_style_radius(indPanel, 8, 0);
  lv_obj_set_style_bg_color(indPanel, lv_color_hex(0x111C2A), 0);
  lv_obj_set_style_border_width(indPanel, 2, 0);
  lv_obj_set_style_border_color(indPanel, lv_color_hex(0x334155), 0);
  lv_obj_set_style_pad_all(indPanel, 0, 0);
  lv_obj_clear_flag(indPanel, LV_OBJ_FLAG_SCROLLABLE);

  // Soap Dispenser Drawer Slot (left of panel)
  lv_obj_t* drawerSlot = lv_obj_create(indPanel);
  lv_obj_set_size(drawerSlot, 46, 26);
  lv_obj_set_pos(drawerSlot, 6, 7);
  lv_obj_set_style_radius(drawerSlot, 4, 0);
  lv_obj_set_style_bg_color(drawerSlot, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_border_width(drawerSlot, 1, 0);
  lv_obj_set_style_border_color(drawerSlot, lv_color_hex(0x475569), 0);
  lv_obj_set_style_pad_all(drawerSlot, 0, 0);
  lv_obj_clear_flag(drawerSlot, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* drawerHandle = lv_obj_create(drawerSlot);
  lv_obj_set_size(drawerHandle, 28, 6);
  lv_obj_align(drawerHandle, LV_ALIGN_CENTER, 0, 2);
  lv_obj_set_style_radius(drawerHandle, 2, 0);
  lv_obj_set_style_bg_color(drawerHandle, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_border_width(drawerHandle, 0, 0);

  // Brand Plate & Mini Cycle Screen (center of panel)
  lv_obj_t* brandLbl = lv_label_create(indPanel);
  lv_label_set_text(brandLbl, "HEAVY DUTY");
  lv_obj_set_style_text_font(brandLbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(brandLbl, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_pos(brandLbl, 58, 12);

  // Pulsing Status LED + Metallic Rotary Dial (right of panel)
  runningStatusLed_ = lv_obj_create(indPanel);
  lv_obj_set_size(runningStatusLed_, 10, 10);
  lv_obj_set_pos(runningStatusLed_, 152, 15);
  lv_obj_set_style_radius(runningStatusLed_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(runningStatusLed_, lv_color_hex(0x10B981), 0);
  lv_obj_set_style_shadow_width(runningStatusLed_, 8, 0);
  lv_obj_set_style_shadow_color(runningStatusLed_, lv_color_hex(0x10B981), 0);
  lv_obj_set_style_border_width(runningStatusLed_, 0, 0);

  lv_obj_t* rotaryKnob = lv_obj_create(indPanel);
  lv_obj_set_size(rotaryKnob, 20, 20);
  lv_obj_set_pos(rotaryKnob, 165, 10);
  lv_obj_set_style_radius(rotaryKnob, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(rotaryKnob, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_border_width(rotaryKnob, 2, 0);
  lv_obj_set_style_border_color(rotaryKnob, lv_color_hex(0x64748B), 0);

  // Heavy-Duty Chrome Outer Door Bezel (164x164 centered at y=58)
  lv_obj_t* outerDoor = lv_obj_create(runningMachine_);
  lv_obj_set_size(outerDoor, 164, 164);
  lv_obj_align(outerDoor, LV_ALIGN_TOP_MID, 0, 58);
  lv_obj_set_style_radius(outerDoor, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(outerDoor, lv_color_hex(0x64748B), 0);
  lv_obj_set_style_bg_grad_color(outerDoor, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_bg_grad_dir(outerDoor, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(outerDoor, 6, 0);
  lv_obj_set_style_border_color(outerDoor, lv_color_hex(0x334155), 0);
  lv_obj_set_style_shadow_width(outerDoor, 16, 0);
  lv_obj_set_style_shadow_opa(outerDoor, LV_OPA_50, 0);
  lv_obj_set_style_shadow_color(outerDoor, lv_color_hex(0x000000), 0);
  lv_obj_set_style_pad_all(outerDoor, 0, 0);
  lv_obj_clear_flag(outerDoor, LV_OBJ_FLAG_SCROLLABLE);

  // Industrial Door Hinge (left) & Heavy Lock Latch Handle (right)
  lv_obj_t* doorHinge = lv_obj_create(runningMachine_);
  lv_obj_set_size(doorHinge, 12, 38);
  lv_obj_set_pos(doorHinge, 12, 121);
  lv_obj_set_style_radius(doorHinge, 4, 0);
  lv_obj_set_style_bg_color(doorHinge, lv_color_hex(0x475569), 0);
  lv_obj_set_style_border_width(doorHinge, 1, 0);
  lv_obj_set_style_border_color(doorHinge, lv_color_hex(0x94A3B8), 0);

  lv_obj_t* doorHandle = lv_obj_create(runningMachine_);
  lv_obj_set_size(doorHandle, 14, 48);
  lv_obj_set_pos(doorHandle, 174, 116);
  lv_obj_set_style_radius(doorHandle, 5, 0);
  lv_obj_set_style_bg_color(doorHandle, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_border_width(doorHandle, 2, 0);
  lv_obj_set_style_border_color(doorHandle, lv_color_hex(0x94A3B8), 0);

  // Deep Glass Porthole Window (132x132 clipped circle inside outerDoor)
  lv_obj_t* glassWindow = lv_obj_create(outerDoor);
  lv_obj_set_size(glassWindow, 132, 132);
  lv_obj_center(glassWindow);
  lv_obj_set_style_radius(glassWindow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(glassWindow, lv_color_hex(0x071324), 0);
  lv_obj_set_style_bg_grad_color(glassWindow, lv_color_hex(0x0F294A), 0);
  lv_obj_set_style_bg_grad_dir(glassWindow, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(glassWindow, 3, 0);
  lv_obj_set_style_border_color(glassWindow, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_pad_all(glassWindow, 0, 0);
  lv_obj_set_style_clip_corner(glassWindow, true, 0);
  lv_obj_clear_flag(glassWindow, LV_OBJ_FLAG_SCROLLABLE);

  // Sloshing Back Water Wave inside Drum
  runningWaterWave2_ = lv_obj_create(glassWindow);
  lv_obj_set_size(runningWaterWave2_, 156, 86);
  lv_obj_set_pos(runningWaterWave2_, -12, 64);
  lv_obj_set_style_radius(runningWaterWave2_, 36, 0);
  lv_obj_set_style_bg_color(runningWaterWave2_, lv_color_hex(0x0284C7), 0);
  lv_obj_set_style_bg_opa(runningWaterWave2_, LV_OPA_70, 0);
  lv_obj_set_style_border_width(runningWaterWave2_, 0, 0);
  lv_obj_clear_flag(runningWaterWave2_, LV_OBJ_FLAG_SCROLLABLE);

  // 3 Tumbling Laundry/Lifter Paddles inside the Drum
  const uint32_t paddleColors[3] = {0x38BDF8, 0xFBBF24, 0xF8FAFC};
  for (uint8_t p = 0; p < 3; ++p) {
    runningPaddles_[p] = lv_obj_create(glassWindow);
    lv_obj_set_size(runningPaddles_[p], 24, 24);
    lv_obj_set_style_radius(runningPaddles_[p], 8, 0);
    lv_obj_set_style_bg_color(runningPaddles_[p], lv_color_hex(paddleColors[p]), 0);
    lv_obj_set_style_bg_opa(runningPaddles_[p], LV_OPA_80, 0);
    lv_obj_set_style_border_width(runningPaddles_[p], 2, 0);
    lv_obj_set_style_border_color(runningPaddles_[p], lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(runningPaddles_[p], LV_OBJ_FLAG_SCROLLABLE);
  }

  // Sloshing Front Water Wave with White Foam Crest
  runningWaterWave1_ = lv_obj_create(glassWindow);
  lv_obj_set_size(runningWaterWave1_, 156, 82);
  lv_obj_set_pos(runningWaterWave1_, -12, 72);
  lv_obj_set_style_radius(runningWaterWave1_, 34, 0);
  lv_obj_set_style_bg_color(runningWaterWave1_, lv_color_hex(0x0EA5E9), 0);
  lv_obj_set_style_bg_grad_color(runningWaterWave1_, lv_color_hex(0x0369A1), 0);
  lv_obj_set_style_bg_grad_dir(runningWaterWave1_, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(runningWaterWave1_, LV_OPA_80, 0);
  lv_obj_set_style_border_width(runningWaterWave1_, 2, 0);
  lv_obj_set_style_border_color(runningWaterWave1_, lv_color_hex(0xBAE6FD), 0);
  lv_obj_clear_flag(runningWaterWave1_, LV_OBJ_FLAG_SCROLLABLE);

  // Outer High-Speed Spinning Stainless Drum Ring
  runningDrum_ = lv_arc_create(glassWindow);
  lv_obj_set_size(runningDrum_, 122, 122);
  lv_obj_center(runningDrum_);
  lv_arc_set_bg_angles(runningDrum_, 0, 360);
  lv_arc_set_angles(runningDrum_, 0, 220);
  lv_obj_set_style_arc_width(runningDrum_, 7, LV_PART_MAIN);
  lv_obj_set_style_arc_color(runningDrum_, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(runningDrum_, LV_OPA_40, LV_PART_MAIN);
  lv_obj_set_style_arc_width(runningDrum_, 7, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(runningDrum_, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(runningDrum_, true, LV_PART_INDICATOR);
  lv_obj_remove_style(runningDrum_, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(runningDrum_, LV_OBJ_FLAG_CLICKABLE);

  // Inner Counter-Rotating Vortex Ring
  runningInnerRing_ = lv_arc_create(glassWindow);
  lv_obj_set_size(runningInnerRing_, 92, 92);
  lv_obj_center(runningInnerRing_);
  lv_arc_set_bg_angles(runningInnerRing_, 0, 360);
  lv_arc_set_angles(runningInnerRing_, 0, 130);
  lv_obj_set_style_arc_width(runningInnerRing_, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(runningInnerRing_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(runningInnerRing_, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(runningInnerRing_, lv_color_hex(0xE0F2FE), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(runningInnerRing_, true, LV_PART_INDICATOR);
  lv_obj_remove_style(runningInnerRing_, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(runningInnerRing_, LV_OBJ_FLAG_CLICKABLE);

  // 5 Suds / Foam Bubbles inside the Porthole Window
  for (uint8_t i = 0; i < 5; ++i) {
    runningBubbles_[i] = createDot(glassWindow, 8 + (i % 3) * 3, 0xE0F2FE);
    lv_obj_set_style_bg_opa(runningBubbles_[i], LV_OPA_80, 0);
  }

  // Bottom Industrial Kickplate Grille with 4 Louver Vents + Drain Filter Cap
  lv_obj_t* kickPlate = lv_obj_create(runningMachine_);
  lv_obj_set_size(kickPlate, 188, 44);
  lv_obj_align(kickPlate, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_radius(kickPlate, 6, 0);
  lv_obj_set_style_bg_color(kickPlate, lv_color_hex(0x64748B), 0);
  lv_obj_set_style_border_width(kickPlate, 1, 0);
  lv_obj_set_style_border_color(kickPlate, lv_color_hex(0x475569), 0);
  lv_obj_set_style_pad_all(kickPlate, 0, 0);
  lv_obj_clear_flag(kickPlate, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t v = 0; v < 3; ++v) {
    lv_obj_t* vent = lv_obj_create(kickPlate);
    lv_obj_set_size(vent, 120, 4);
    lv_obj_set_pos(vent, 10, 9 + v * 10);
    lv_obj_set_style_radius(vent, 2, 0);
    lv_obj_set_style_bg_color(vent, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(vent, 0, 0);
  }

  lv_obj_t* drainCap = lv_obj_create(kickPlate);
  lv_obj_set_size(drainCap, 28, 28);
  lv_obj_align(drainCap, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_style_radius(drainCap, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(drainCap, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(drainCap, 2, 0);
  lv_obj_set_style_border_color(drainCap, lv_color_hex(0x94A3B8), 0);

  // RIGHT SIDE: Industrial Digital Telemetry & Countdown Console Card (240x292 at x=228, y=14)
  lv_obj_t* runConsole = lv_obj_create(running_.native());
  lv_obj_set_size(runConsole, 240, 292);
  lv_obj_set_pos(runConsole, 228, 14);
  lv_obj_set_style_radius(runConsole, 14, 0);
  lv_obj_set_style_bg_color(runConsole, lv_color_hex(0x111E2E), 0);
  lv_obj_set_style_bg_grad_color(runConsole, lv_color_hex(0x0B1420), 0);
  lv_obj_set_style_bg_grad_dir(runConsole, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(runConsole, 2, 0);
  lv_obj_set_style_border_color(runConsole, lv_color_hex(0x263B54), 0);
  lv_obj_set_style_shadow_width(runConsole, 20, 0);
  lv_obj_set_style_shadow_opa(runConsole, LV_OPA_50, 0);
  lv_obj_set_style_shadow_color(runConsole, lv_color_hex(0x000000), 0);
  lv_obj_set_style_pad_all(runConsole, 0, 0);
  lv_obj_clear_flag(runConsole, LV_OBJ_FLAG_SCROLLABLE);

  // Active Temperature Mode Badge Pill at top of Console
  runningTempPill_ = lv_obj_create(runConsole);
  lv_obj_set_size(runningTempPill_, 216, 38);
  lv_obj_align(runningTempPill_, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_radius(runningTempPill_, 10, 0);
  lv_obj_set_style_bg_color(runningTempPill_, lv_color_hex(0x0284C7), 0);
  lv_obj_set_style_border_width(runningTempPill_, 1, 0);
  lv_obj_set_style_border_color(runningTempPill_, lv_color_hex(0x7DD3FC), 0);
  lv_obj_set_style_pad_all(runningTempPill_, 0, 0);
  lv_obj_clear_flag(runningTempPill_, LV_OBJ_FLAG_SCROLLABLE);

  runningTempLabel_ = lv_label_create(runningTempPill_);
  lv_label_set_text(runningTempLabel_, shapeLaoText("ໂໝດຊັກ: ນ້ຳເຢັນ COLD").c_str());
  lv_obj_set_style_text_font(runningTempLabel_, &notosanslao_shaped_20, 0);
  lv_obj_set_style_text_color(runningTempLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(runningTempLabel_);

  // Large Industrial Seven-Segment Style Digital Countdown Screen
  lv_obj_t* runningScreen = lv_obj_create(runConsole);
  lv_obj_set_size(runningScreen, 216, 102);
  lv_obj_align(runningScreen, LV_ALIGN_TOP_MID, 0, 56);
  lv_obj_set_style_radius(runningScreen, 10, 0);
  lv_obj_set_style_bg_color(runningScreen, lv_color_hex(0x060D16), 0);
  lv_obj_set_style_border_width(runningScreen, 2, 0);
  lv_obj_set_style_border_color(runningScreen, lv_color_hex(0x1E3A5F), 0);
  lv_obj_set_style_pad_all(runningScreen, 0, 0);
  lv_obj_clear_flag(runningScreen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* remHeader = lv_label_create(runningScreen);
  lv_label_set_text(remHeader, "TIME REMAINING (MM:SS)");
  lv_obj_set_style_text_font(remHeader, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(remHeader, lv_color_hex(0x64748B), 0);
  lv_obj_align(remHeader, LV_ALIGN_TOP_MID, 0, 6);

  countdownText_.parent(running_);
  lv_obj_set_parent(countdownText_.native(), runningScreen);
  countdownText_.font(&lv_font_montserrat_48);
  countdownText_.textColor(0x00F0FF);
  countdownText_.text("00:00");
  countdownText_.align(Align::BottomCenter, 0, -6);

  // Animated Wash Progress Bar
  runningProgressBar_ = lv_bar_create(runConsole);
  lv_obj_set_size(runningProgressBar_, 216, 12);
  lv_obj_align(runningProgressBar_, LV_ALIGN_TOP_MID, 0, 168);
  lv_bar_set_range(runningProgressBar_, 0, 100);
  lv_bar_set_value(runningProgressBar_, 50, LV_ANIM_OFF);
  lv_obj_set_style_radius(runningProgressBar_, 6, LV_PART_MAIN);
  lv_obj_set_style_bg_color(runningProgressBar_, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_radius(runningProgressBar_, 6, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(runningProgressBar_, lv_color_hex(0x06B6D4), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(runningProgressBar_, lv_color_hex(0x10B981), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(runningProgressBar_, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);

  // Lao & English Status Text inside Console
  runningText_.parent(running_);
  lv_obj_set_parent(runningText_.native(), runConsole);
  runningText_.font(&notosanslao_shaped_20);
  runningText_.textColor(0xE2E8F0);
  runningText_.size(220, 56);
  runningText_.align(Align::BottomCenter, 0, -32);
  lv_obj_set_style_text_align(runningText_.native(), LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(runningText_.native(), 2, 0);

  lv_obj_t* indSub = lv_label_create(runConsole);
  lv_label_set_text(indSub, "WASHING IN PROGRESS");
  lv_obj_set_style_text_font(indSub, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(indSub, lv_color_hex(0x10B981), 0);
  lv_obj_align(indSub, LV_ALIGN_BOTTOM_MID, 0, -10);

  configurePage(success_, 0xF3FAFD); // Same light background as running page
  successMachine_ = createWasherMachine(success_.native(), 155, 30, 170, 202, 0xFFFFFF, &successDrum_);
  
  // Glowing emerald indicator arc on success drum
  lv_obj_set_style_arc_opa(successDrum_, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(successDrum_, lv_color_hex(0x12B76A), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(successDrum_, lv_color_hex(0xD1FAE5), LV_PART_MAIN);

  // Pulsing halo ring inside success drum
  successHalo_ = lv_obj_create(successDrum_);
  lv_obj_set_size(successHalo_, 96, 96);
  lv_obj_align(successHalo_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(successHalo_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(successHalo_, lv_color_hex(0xA7F3D0), 0);
  lv_obj_set_style_bg_opa(successHalo_, LV_OPA_60, 0);
  lv_obj_set_style_border_width(successHalo_, 0, 0);
  lv_obj_clear_flag(successHalo_, LV_OBJ_FLAG_SCROLLABLE);

  // Solid green circle inside success drum
  lv_obj_t* successCircle = lv_obj_create(successDrum_);
  lv_obj_set_size(successCircle, 78, 78);
  lv_obj_align(successCircle, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(successCircle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(successCircle, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(successCircle, lv_color_hex(0x12B76A), 0);
  lv_obj_set_style_border_width(successCircle, 2, 0);
  lv_obj_set_style_border_color(successCircle, lv_color_hex(0xFFFFFF), 0);
  lv_obj_clear_flag(successCircle, LV_OBJ_FLAG_SCROLLABLE);

  // Large white checkmark in the center of the green circle
  successIcon_.parent(success_);
  lv_obj_set_parent(successIcon_.native(), successCircle);
  successIcon_.font(&lv_font_montserrat_36);
  successIcon_.textColor(0xFFFFFF);
  successIcon_.text(LV_SYMBOL_OK);
  successIcon_.align(Align::Center, 0, 0);

  // Floating celebration sparkles around the washer machine on Success page
  const uint32_t winSparkColors[4] = {0x12B76A, 0xFFD34E, 0x38BDF8, 0x10B981};
  for (uint8_t i = 0; i < 4; ++i) {
    successSparks_[i] = createDot(success_.native(), 12 + (i % 2) * 3, winSparkColors[i]);
  }

  successText_.parent(success_);
  successText_.font(&notosanslao_shaped_20);
  successText_.textColor(0x0B2239);
  successText_.size(460, 48);
  successText_.align(Align::BottomCenter, 0, -24);
  lv_obj_set_style_text_align(successText_.native(), LV_TEXT_ALIGN_CENTER, 0);

  configurePage(pause_, 0xF3FAFD); // Same light background as running page
  pauseMachine_ = createWasherMachine(pause_.native(), 155, 30, 170, 202, 0xFFFFFF, &pauseDrum_);
  lv_obj_set_style_arc_color(pauseDrum_, lv_color_hex(0xE85D55), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(pauseDrum_, lv_color_hex(0xFEE2E2), LV_PART_MAIN);

  pauseHalo_ = lv_obj_create(pauseDrum_);
  lv_obj_set_size(pauseHalo_, 96, 96);
  lv_obj_align(pauseHalo_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(pauseHalo_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(pauseHalo_, lv_color_hex(0xFECACA), 0);
  lv_obj_set_style_bg_opa(pauseHalo_, LV_OPA_60, 0);
  lv_obj_set_style_border_width(pauseHalo_, 0, 0);
  lv_obj_clear_flag(pauseHalo_, LV_OBJ_FLAG_SCROLLABLE);

  // Solid red circle inside pause drum
  lv_obj_t* pauseCircle = lv_obj_create(pauseDrum_);
  lv_obj_set_size(pauseCircle, 78, 78);
  lv_obj_align(pauseCircle, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(pauseCircle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(pauseCircle, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(pauseCircle, lv_color_hex(0xDC2626), 0);
  lv_obj_set_style_border_width(pauseCircle, 2, 0);
  lv_obj_set_style_border_color(pauseCircle, lv_color_hex(0xFFFFFF), 0);
  lv_obj_clear_flag(pauseCircle, LV_OBJ_FLAG_SCROLLABLE);

  // Large white cross in the center of the red circle
  pauseIcon_.parent(pause_);
  lv_obj_set_parent(pauseIcon_.native(), pauseCircle);
  pauseIcon_.font(&lv_font_montserrat_36);
  pauseIcon_.textColor(0xFFFFFF);
  pauseIcon_.text(LV_SYMBOL_CLOSE);
  pauseIcon_.align(Align::Center, 0, 0);

  pauseText_.parent(pause_);
  pauseText_.font(&notosanslao_shaped_20); // Reduced font size to 20px
  pauseText_.textColor(0x1C2B36); // Dark color
  pauseText_.size(460, 48);
  pauseText_.align(Align::BottomCenter, 0, -9); // Moved down by 15px (from -24 to -9) to show full vowels/tone marks
  lv_obj_set_style_text_align(pauseText_.native(), LV_TEXT_ALIGN_CENTER, 0);
}

void WasherView::buildPinPage() {
  configurePage(pinPage_, 0x101820);
  pinBack_.parent(pinPage_);
  pinBack_.text("<  ຍ້ອນກັບ");
  pinBack_.label().font(&notosanslao_shaped_25);
  pinBack_.size(120, 44);
  pinBack_.align(Align::TopLeft, 5, 7);
  pinBack_.color(0x263746);
  pinBack_.onClicked(backClicked, this);
  pinTitle_.parent(pinPage_);
  pinTitle_.font(&notosanslao_shaped_25);
  pinTitle_.textColor(0xFFFFFF);
  pinTitle_.align(Align::TopCenter, 0, 8);
  pinDots_.parent(pinPage_);
  pinDots_.font(&lv_font_montserrat_20);
  pinDots_.text("_ _ _ _");
  pinDots_.textColor(0x52D3A5);
  pinDots_.align(Align::TopCenter, 0, 52);
  pinError_.parent(pinPage_);
  pinError_.font(&notosanslao_shaped_25);
  pinError_.textColor(0xFF6B62);
  pinError_.align(Align::BottomCenter, 0, -4);

  const char keys[12] = {'1','2','3','4','5','6','7','8','9','C','0','E'};
  for (uint8_t i = 0; i < 12; ++i) {
    lv_obj_t* button = lv_btn_create(pinPage_.native());
    pinKeys_[i] = button;
    lv_obj_set_size(button, 78, 44);
    lv_obj_set_pos(button, 113 + (i % 3) * 86, 92 + (i / 3) * 50);
    lv_obj_set_style_radius(button, 5, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(keys[i] == 'E' ? 0x24A148 : 0x263746), 0);
    lv_obj_add_event_cb(button, pinKeyEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(keys[i])));
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, keys[i] == 'E' ? LV_SYMBOL_OK : (keys[i] == 'C' ? LV_SYMBOL_CLOSE : String(keys[i]).c_str()));
    lv_obj_center(label);
  }
}

lv_obj_t* WasherView::makeLabel(lv_obj_t* parent, const char* text, int x, int y, const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(parent);
  const String shapedText = shapeLaoText(text);
  lv_label_set_text(label, shapedText.c_str());
  lv_obj_set_pos(label, x, y);
  lv_obj_set_style_text_color(label, lv_color_hex(0x1C2B36), 0);
  if (font) lv_obj_set_style_text_font(label, font, 0);
  return label;
}

lv_obj_t* WasherView::makeTextArea(lv_obj_t* parent, int x, int y, int width, const char* text, bool password) {
  lv_obj_t* input = lv_textarea_create(parent);
  lv_obj_set_size(input, width, 40);
  lv_obj_set_pos(input, x, y);
  lv_textarea_set_one_line(input, true);
  lv_textarea_set_text(input, text ? text : "");
  lv_textarea_set_password_mode(input, password);
  return input;
}

void WasherView::buildSettings() {
  configurePage(settingsPage_, 0xE9EEF2);
  settingsBack_.parent(settingsPage_);
  settingsBack_.text("EXIT");
  settingsBack_.label().font(&lv_font_montserrat_20);
  settingsBack_.size(96, 40);
  settingsBack_.align(Align::TopLeft, 4, 5);
  settingsBack_.color(0xC73934);
  settingsBack_.onClicked(backClicked, this);
  saveButton_.parent(settingsPage_);
  saveButton_.size(44, 38);
  saveButton_.align(Align::TopRight, -4, 3);
  saveButton_.color(0x24A148);
  saveButton_.onClicked(saveClicked, this);
  lv_obj_t* title = makeLabel(settingsPage_.native(), "SETTINGS", 204, 13, &lv_font_montserrat_14);
  lv_obj_set_style_text_color(title, lv_color_hex(0x1C2B36), 0);

  tabView_ = lv_tabview_create(settingsPage_.native(), LV_DIR_TOP, 38);
  lv_obj_set_size(tabView_, 480, 276);
  lv_obj_set_pos(tabView_, 0, 44);
  lv_obj_set_style_radius(tabView_, 0, 0);
  lv_obj_t* wifiTab = lv_tabview_add_tab(tabView_, "WiFi");
  lv_obj_t* pinTab = lv_tabview_add_tab(tabView_, "PIN");
  lv_obj_t* optionTab = lv_tabview_add_tab(tabView_, "OPTION");
  lv_obj_t* terminalTab = lv_tabview_add_tab(tabView_, "TID");
  lv_obj_t* timerTab = lv_tabview_add_tab(tabView_, "TIMER");
  lv_obj_t* displayTab = lv_tabview_add_tab(tabView_, "DISPLAY");

  // WiFi 1
  makeLabel(wifiTab, "WiFi 1", 8, 4, &lv_font_montserrat_14);
  
  wifiSwitch_[0] = lv_switch_create(wifiTab);
  lv_obj_set_size(wifiSwitch_[0], 38, 22);
  lv_obj_set_pos(wifiSwitch_[0], 56, 2);
  if (settings_->wifiEnabled1) lv_obj_add_state(wifiSwitch_[0], LV_STATE_CHECKED);
  lv_obj_add_event_cb(wifiSwitch_[0], wifiSwitchEvent, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void*>(0));

  wifiBadge_[0] = makeLabel(wifiTab, "OFFLINE", 98, 4, &lv_font_montserrat_14);
  lv_obj_set_style_text_color(wifiBadge_[0], lv_color_hex(0x8A9BA8), 0);

  wifiScanBtn_[0] = lv_btn_create(wifiTab);
  lv_obj_set_size(wifiScanBtn_[0], 66, 28);
  lv_obj_set_pos(wifiScanBtn_[0], 158, 0);
  lv_obj_set_style_bg_color(wifiScanBtn_[0], lv_color_hex(0x1686C3), 0);
  lv_obj_add_event_cb(wifiScanBtn_[0], wifiScanEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(0));
  lv_obj_t* scanLabel1 = lv_label_create(wifiScanBtn_[0]);
  lv_label_set_text(scanLabel1, shapeLaoText("ຄົ້ນຫາ").c_str());
  lv_obj_set_style_text_font(scanLabel1, &notosanslao_shaped_20, 0);
  lv_obj_center(scanLabel1);

  wifiSsidInput_ = makeTextArea(wifiTab, 8, 34, 216, settings_->wifiSsid.c_str());

  wifiPasswordInput_ = makeTextArea(wifiTab, 8, 76, 174, settings_->wifiPassword.c_str(), true);
  wifiPassToggle_[0] = lv_btn_create(wifiTab);
  lv_obj_set_size(wifiPassToggle_[0], 38, 40);
  lv_obj_set_pos(wifiPassToggle_[0], 186, 76);
  lv_obj_set_style_bg_color(wifiPassToggle_[0], lv_color_hex(0x506271), 0);
  lv_obj_add_event_cb(wifiPassToggle_[0], wifiPassToggleEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(0));
  lv_obj_t* eyeLabel1 = lv_label_create(wifiPassToggle_[0]);
  lv_label_set_text(eyeLabel1, LV_SYMBOL_EYE_OPEN);
  lv_obj_center(eyeLabel1);

  lv_obj_t* useBtn1 = lv_btn_create(wifiTab);
  lv_obj_set_size(useBtn1, 216, 32);
  lv_obj_set_pos(useBtn1, 8, 118);
  lv_obj_set_style_bg_color(useBtn1, lv_color_hex(0x24A148), 0);
  lv_obj_add_event_cb(useBtn1, wifiConnectEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(0));
  lv_obj_t* useLabel1 = lv_label_create(useBtn1);
  lv_label_set_text(useLabel1, shapeLaoText("ເຊື່ອມຕໍ່ WiFi 1").c_str());
  lv_obj_set_style_text_font(useLabel1, &notosanslao_shaped_20, 0);
  lv_obj_center(useLabel1);

  // WiFi 2
  makeLabel(wifiTab, "WiFi 2", 236, 4, &lv_font_montserrat_14);

  wifiSwitch_[1] = lv_switch_create(wifiTab);
  lv_obj_set_size(wifiSwitch_[1], 38, 22);
  lv_obj_set_pos(wifiSwitch_[1], 284, 2);
  if (settings_->wifiEnabled2) lv_obj_add_state(wifiSwitch_[1], LV_STATE_CHECKED);
  lv_obj_add_event_cb(wifiSwitch_[1], wifiSwitchEvent, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void*>(1));

  wifiBadge_[1] = makeLabel(wifiTab, "OFFLINE", 326, 4, &lv_font_montserrat_14);
  lv_obj_set_style_text_color(wifiBadge_[1], lv_color_hex(0x8A9BA8), 0);

  wifiScanBtn_[1] = lv_btn_create(wifiTab);
  lv_obj_set_size(wifiScanBtn_[1], 66, 28);
  lv_obj_set_pos(wifiScanBtn_[1], 386, 0);
  lv_obj_set_style_bg_color(wifiScanBtn_[1], lv_color_hex(0x1686C3), 0);
  lv_obj_add_event_cb(wifiScanBtn_[1], wifiScanEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(1));
  lv_obj_t* scanLabel2 = lv_label_create(wifiScanBtn_[1]);
  lv_label_set_text(scanLabel2, shapeLaoText("ຄົ້ນຫາ").c_str());
  lv_obj_set_style_text_font(scanLabel2, &notosanslao_shaped_20, 0);
  lv_obj_center(scanLabel2);

  wifiSsid2Input_ = makeTextArea(wifiTab, 236, 34, 216, settings_->wifiSsid2.c_str());

  wifiPassword2Input_ = makeTextArea(wifiTab, 236, 76, 174, settings_->wifiPassword2.c_str(), true);
  wifiPassToggle_[1] = lv_btn_create(wifiTab);
  lv_obj_set_size(wifiPassToggle_[1], 38, 40);
  lv_obj_set_pos(wifiPassToggle_[1], 414, 76);
  lv_obj_set_style_bg_color(wifiPassToggle_[1], lv_color_hex(0x506271), 0);
  lv_obj_add_event_cb(wifiPassToggle_[1], wifiPassToggleEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(1));
  lv_obj_t* eyeLabel2 = lv_label_create(wifiPassToggle_[1]);
  lv_label_set_text(eyeLabel2, LV_SYMBOL_EYE_OPEN);
  lv_obj_center(eyeLabel2);

  lv_obj_t* useBtn2 = lv_btn_create(wifiTab);
  lv_obj_set_size(useBtn2, 216, 32);
  lv_obj_set_pos(useBtn2, 236, 118);
  lv_obj_set_style_bg_color(useBtn2, lv_color_hex(0x24A148), 0);
  lv_obj_add_event_cb(useBtn2, wifiConnectEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(1));
  lv_obj_t* useLabel2 = lv_label_create(useBtn2);
  lv_label_set_text(useLabel2, shapeLaoText("ເຊື່ອມຕໍ່ WiFi 2").c_str());
  lv_obj_set_style_text_font(useLabel2, &notosanslao_shaped_20, 0);
  lv_obj_center(useLabel2);

  wifiStatusLabel_ = makeLabel(wifiTab, "ສະຖານະ: ບໍ່ໄດ້ເຊື່ອມຕໍ່ (Offline)", 8, 156, &notosanslao_shaped_20);
  lv_obj_set_style_text_color(wifiStatusLabel_, lv_color_hex(0xE85D55), 0);

  makeLabel(pinTab, "ຕັ້ງລະຫັດ PIN ໃໝ່", 12, 10, &notosanslao_shaped_25);
  pinInput_ = makeTextArea(pinTab, 12, 60, 220, settings_->settingsPin.c_str(), true);
  lv_textarea_set_max_length(pinInput_, 8);
  makeLabel(pinTab, "ກົດ SAVE ເພື່ອບັນທຶກ", 12, 115, &notosanslao_shaped_25);

  // New OPTION Tab: Option Selector (1, 2, 3) + Pulse Count Adjuster ([-5] [-1] [ VAL ] [+1] [+5]) + TEST Button
  const char* optNames[3] = {"OPTION 1", "OPTION 2", "OPTION 3"};
  for (uint8_t i = 0; i < 3; ++i) {
    optionTabBtns_[i] = lv_btn_create(optionTab);
    lv_obj_set_size(optionTabBtns_[i], 140, 36);
    lv_obj_set_pos(optionTabBtns_[i], 10 + i * 150, 6);
    lv_obj_set_style_radius(optionTabBtns_[i], 6, 0);
    lv_obj_set_style_bg_color(optionTabBtns_[i], lv_color_hex(i == 0 ? 0x1686C3 : 0x263746), 0);
    lv_obj_add_event_cb(optionTabBtns_[i], optionTabSelectEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    lv_obj_t* btnLbl = lv_label_create(optionTabBtns_[i]);
    lv_label_set_text(btnLbl, optNames[i]);
    lv_obj_set_style_text_font(btnLbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btnLbl);
  }

  // Value display and adjustment card
  lv_obj_t* optCard = lv_obj_create(optionTab);
  lv_obj_set_size(optCard, 442, 106);
  lv_obj_set_pos(optCard, 10, 48);
  lv_obj_set_style_bg_color(optCard, lv_color_hex(0x1B2A38), 0);
  lv_obj_set_style_border_color(optCard, lv_color_hex(0x2D3E4F), 0);
  lv_obj_set_style_border_width(optCard, 1, 0);
  lv_obj_set_style_radius(optCard, 8, 0);
  lv_obj_set_style_pad_all(optCard, 6, 0);
  lv_obj_clear_flag(optCard, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* optTitle = lv_label_create(optCard);
  lv_label_set_text(optTitle, shapeLaoText("ຈຳນວນກົດ Pulse (Relay):").c_str());
  lv_obj_set_style_text_font(optTitle, &notosanslao_shaped_20, 0);
  lv_obj_set_style_text_color(optTitle, lv_color_hex(0x52D3A5), 0);
  lv_obj_set_pos(optTitle, 8, 4);

  // Buttons: [-5] [-1] [ VALUE ] [+1] [+5] [ TEST ]
  const struct { const char* label; int diff; int x; uint32_t color; } adjBtns[] = {
    {"-5", -5, 6, 0x263746},
    {"-1", -1, 68, 0x263746},
    {"+1", +1, 236, 0x263746},
    {"+5", +5, 298, 0x263746},
  };

  for (int i = 0; i < 4; ++i) {
    lv_obj_t* b = lv_btn_create(optCard);
    lv_obj_set_size(b, 56, 42);
    lv_obj_set_pos(b, adjBtns[i].x, 46);
    lv_obj_set_style_bg_color(b, lv_color_hex(adjBtns[i].color), 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_add_event_cb(b, optionPulseAdjustEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(adjBtns[i].diff)));
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, adjBtns[i].label);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_center(l);
  }

  // Center Value Display Box
  lv_obj_t* valBox = lv_obj_create(optCard);
  lv_obj_set_size(valBox, 98, 42);
  lv_obj_set_pos(valBox, 130, 46);
  lv_obj_set_style_bg_color(valBox, lv_color_hex(0x101820), 0);
  lv_obj_set_style_border_color(valBox, lv_color_hex(0x20A765), 0);
  lv_obj_set_style_border_width(valBox, 2, 0);
  lv_obj_set_style_radius(valBox, 6, 0);
  lv_obj_clear_flag(valBox, LV_OBJ_FLAG_SCROLLABLE);

  optionPulseLabel_ = lv_label_create(valBox);
  lv_label_set_text_fmt(optionPulseLabel_, "%u", settings_->pulseCount[0]);
  lv_obj_set_style_text_font(optionPulseLabel_, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(optionPulseLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(optionPulseLabel_);

  // TEST Button
  lv_obj_t* testBtn = lv_btn_create(optCard);
  lv_obj_set_size(testBtn, 66, 42);
  lv_obj_set_pos(testBtn, 360, 46);
  lv_obj_set_style_bg_color(testBtn, lv_color_hex(0xE05D44), 0);
  lv_obj_set_style_radius(testBtn, 6, 0);
  lv_obj_add_event_cb(testBtn, optionTestRelayEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* testLbl = lv_label_create(testBtn);
  lv_label_set_text(testLbl, "TEST");
  lv_obj_set_style_text_font(testLbl, &lv_font_montserrat_14, 0);
  lv_obj_center(testLbl);

  // Bottom note
  lv_obj_t* optNote = lv_label_create(optionTab);
  lv_label_set_text(optNote, shapeLaoText("ກົດປຸ່ມ SAVE ດ້ານເທິງຂວາ ເພື່ອບັນທຶກຄ່າ").c_str());
  lv_obj_set_style_text_font(optNote, &notosanslao_shaped_20, 0);
  lv_obj_set_style_text_color(optNote, lv_color_hex(0x8A9BA8), 0);
  lv_obj_set_pos(optNote, 14, 166);

  makeLabel(terminalTab, "TERMINAL ID", 12, 16, &lv_font_montserrat_14);
  terminalInput_ = makeTextArea(terminalTab, 12, 52, 280, settings_->terminalId.c_str());
  makeLabel(terminalTab, "ຄ່ານີ້ຕ້ອງກົງກັບ server", 12, 105, &notosanslao_shaped_25);

  String timeOptions;
  for (uint16_t n = 1; n <= 180; ++n) { if (n > 1) timeOptions += '\n'; timeOptions += n; }
  for (uint8_t i = 0; i < 3; ++i) {
    makeLabel(timerTab, ("OPTION " + String(i + 1)).c_str(), 8, 8 + i * 55);
    timeDropdown_[i] = lv_dropdown_create(timerTab);
    lv_dropdown_set_options(timeDropdown_[i], timeOptions.c_str());
    lv_dropdown_set_selected(timeDropdown_[i], settings_->durationMinutes[i] - 1);
    lv_obj_set_size(timeDropdown_[i], 120, 40);
    lv_obj_set_pos(timeDropdown_[i], 130, i * 55);
    makeLabel(timerTab, "ນາທີ", 270, 8 + i * 55, &notosanslao_shaped_25);
  }

  lv_obj_set_style_pad_all(displayTab, 8, 0);
  lv_obj_clear_flag(displayTab, LV_OBJ_FLAG_SCROLLABLE);

  // ---- Brightness Control Row ----
  makeLabel(displayTab, "ປັບຄວາມສະຫວ່າງຈໍ", 8, 2, &notosanslao_shaped_20);
  brightnessValue_ = makeLabel(displayTab, String(settings_->brightness).c_str(), 400, 4, &lv_font_montserrat_20);

  lv_obj_t* brMinusBtn = lv_btn_create(displayTab);
  lv_obj_set_size(brMinusBtn, 52, 40);
  lv_obj_set_pos(brMinusBtn, 8, 42);
  lv_obj_set_style_bg_color(brMinusBtn, lv_color_hex(0x263746), 0);
  lv_obj_set_style_radius(brMinusBtn, 6, 0);
  lv_obj_add_event_cb(brMinusBtn, brightnessStepEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(-15)));
  lv_obj_t* brMinusLbl = lv_label_create(brMinusBtn);
  lv_label_set_text(brMinusLbl, LV_SYMBOL_MINUS);
  lv_obj_center(brMinusLbl);

  brightnessSlider_ = lv_slider_create(displayTab);
  lv_obj_set_size(brightnessSlider_, 310, 18);
  lv_obj_set_pos(brightnessSlider_, 76, 53);
  lv_slider_set_range(brightnessSlider_, 10, 255);
  lv_slider_set_value(brightnessSlider_, settings_->brightness, LV_ANIM_OFF);
  lv_obj_add_event_cb(brightnessSlider_, brightnessEvent, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_t* brPlusBtn = lv_btn_create(displayTab);
  lv_obj_set_size(brPlusBtn, 52, 40);
  lv_obj_set_pos(brPlusBtn, 402, 42);
  lv_obj_set_style_bg_color(brPlusBtn, lv_color_hex(0x1686C3), 0);
  lv_obj_set_style_radius(brPlusBtn, 6, 0);
  lv_obj_add_event_cb(brPlusBtn, brightnessStepEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(+15)));
  lv_obj_t* brPlusLbl = lv_label_create(brPlusBtn);
  lv_label_set_text(brPlusLbl, LV_SYMBOL_PLUS);
  lv_obj_center(brPlusLbl);

  // ---- Volume Control Row ----
  makeLabel(displayTab, "ປັບລະດັບສຽງບິບ", 8, 102, &notosanslao_shaped_20);
  volumeValue_ = makeLabel(displayTab, (String(settings_->volume) + "%").c_str(), 394, 104, &lv_font_montserrat_20);

  lv_obj_t* volMinusBtn = lv_btn_create(displayTab);
  lv_obj_set_size(volMinusBtn, 52, 40);
  lv_obj_set_pos(volMinusBtn, 8, 142);
  lv_obj_set_style_bg_color(volMinusBtn, lv_color_hex(0x263746), 0);
  lv_obj_set_style_radius(volMinusBtn, 6, 0);
  lv_obj_add_event_cb(volMinusBtn, volumeStepEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(-10)));
  lv_obj_t* volMinusLbl = lv_label_create(volMinusBtn);
  lv_label_set_text(volMinusLbl, LV_SYMBOL_MINUS);
  lv_obj_center(volMinusLbl);

  volumeSlider_ = lv_slider_create(displayTab);
  lv_obj_set_size(volumeSlider_, 310, 18);
  lv_obj_set_pos(volumeSlider_, 76, 153);
  lv_slider_set_range(volumeSlider_, 0, 100);
  lv_slider_set_value(volumeSlider_, settings_->volume, LV_ANIM_OFF);
  lv_obj_add_event_cb(volumeSlider_, volumeEvent, LV_EVENT_ALL, this);

  lv_obj_t* volPlusBtn = lv_btn_create(displayTab);
  lv_obj_set_size(volPlusBtn, 52, 40);
  lv_obj_set_pos(volPlusBtn, 402, 142);
  lv_obj_set_style_bg_color(volPlusBtn, lv_color_hex(0x20A765), 0);
  lv_obj_set_style_radius(volPlusBtn, 6, 0);
  lv_obj_add_event_cb(volPlusBtn, volumeStepEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(+10)));
  lv_obj_t* volPlusLbl = lv_label_create(volPlusBtn);
  lv_label_set_text(volPlusLbl, LV_SYMBOL_PLUS);
  lv_obj_center(volPlusLbl);

  lv_obj_t* dispNote = makeLabel(displayTab, "ກົດປຸ່ມ SAVE ດ້ານເທິງຂວາ ເພື່ອບັນທຶກຄ່າ", 8, 192, &notosanslao_shaped_20);
  lv_obj_set_style_text_color(dispNote, lv_color_hex(0x566573), 0);

  lv_obj_add_event_cb(wifiSsidInput_, textAreaEvent, LV_EVENT_FOCUSED, reinterpret_cast<void*>(0));
  lv_obj_add_event_cb(wifiPasswordInput_, textAreaEvent, LV_EVENT_FOCUSED, reinterpret_cast<void*>(0));
  lv_obj_add_event_cb(wifiSsid2Input_, textAreaEvent, LV_EVENT_FOCUSED, reinterpret_cast<void*>(0));
  lv_obj_add_event_cb(wifiPassword2Input_, textAreaEvent, LV_EVENT_FOCUSED, reinterpret_cast<void*>(0));
  lv_obj_add_event_cb(pinInput_, textAreaEvent, LV_EVENT_FOCUSED, reinterpret_cast<void*>(1));
  lv_obj_add_event_cb(terminalInput_, textAreaEvent, LV_EVENT_FOCUSED, reinterpret_cast<void*>(1));

  keyboard_ = lv_keyboard_create(settingsPage_.native());
  lv_obj_set_size(keyboard_, 480, 176);
  lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(keyboard_, keyboardEvent, LV_EVENT_ALL, this);
  lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);

  keyboardPreviewCont_ = lv_obj_create(settingsPage_.native());
  lv_obj_set_size(keyboardPreviewCont_, 470, 48);
  lv_obj_set_pos(keyboardPreviewCont_, 5, 92);
  lv_obj_set_style_bg_color(keyboardPreviewCont_, lv_color_hex(0x1B2A38), 0);
  lv_obj_set_style_border_color(keyboardPreviewCont_, lv_color_hex(0x20A765), 0);
  lv_obj_set_style_border_width(keyboardPreviewCont_, 2, 0);
  lv_obj_set_style_radius(keyboardPreviewCont_, 6, 0);
  lv_obj_set_style_pad_all(keyboardPreviewCont_, 4, 0);
  lv_obj_clear_flag(keyboardPreviewCont_, LV_OBJ_FLAG_SCROLLABLE);

  keyboardPreviewInput_ = lv_textarea_create(keyboardPreviewCont_);
  lv_obj_set_size(keyboardPreviewInput_, 458, 36);
  lv_obj_set_pos(keyboardPreviewInput_, 2, 2);
  lv_textarea_set_one_line(keyboardPreviewInput_, true);
  lv_obj_set_style_text_font(keyboardPreviewInput_, &lv_font_montserrat_14, 0);
  lv_obj_add_flag(keyboardPreviewCont_, LV_OBJ_FLAG_HIDDEN);

  // Dimmed Backdrop for Save Confirmation Modal
  saveBackdrop_ = lv_obj_create(settingsPage_.native());
  lv_obj_set_size(saveBackdrop_, 480, 320);
  lv_obj_set_pos(saveBackdrop_, 0, 0);
  lv_obj_set_style_radius(saveBackdrop_, 0, 0);
  lv_obj_set_style_bg_color(saveBackdrop_, lv_color_hex(0x06101A), 0);
  lv_obj_set_style_bg_opa(saveBackdrop_, LV_OPA_50, 0);
  lv_obj_set_style_border_width(saveBackdrop_, 0, 0);
  lv_obj_clear_flag(saveBackdrop_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(saveBackdrop_, LV_OBJ_FLAG_HIDDEN);

  // Save Confirmation Modal
  saveModal_ = lv_obj_create(settingsPage_.native());
  lv_obj_set_size(saveModal_, 328, 168);
  lv_obj_align(saveModal_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(saveModal_, lv_color_hex(0x162536), 0);
  lv_obj_set_style_bg_grad_color(saveModal_, lv_color_hex(0x0D1722), 0);
  lv_obj_set_style_bg_grad_dir(saveModal_, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_color(saveModal_, lv_color_hex(0x12B76A), 0);
  lv_obj_set_style_border_width(saveModal_, 2, 0);
  lv_obj_set_style_radius(saveModal_, 16, 0);
  lv_obj_set_style_shadow_width(saveModal_, 30, 0);
  lv_obj_set_style_shadow_color(saveModal_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(saveModal_, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(saveModal_, 0, 0);
  lv_obj_clear_flag(saveModal_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* saveAccent = lv_obj_create(saveModal_);
  lv_obj_set_size(saveAccent, 324, 5);
  lv_obj_align(saveAccent, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(saveAccent, 3, 0);
  lv_obj_set_style_bg_color(saveAccent, lv_color_hex(0x12B76A), 0);
  lv_obj_set_style_border_width(saveAccent, 0, 0);
  lv_obj_clear_flag(saveAccent, LV_OBJ_FLAG_SCROLLABLE);

  // Dual-ring modern emerald icon badge
  lv_obj_t* modalRing = lv_obj_create(saveModal_);
  lv_obj_set_size(modalRing, 58, 58);
  lv_obj_align(modalRing, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_radius(modalRing, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(modalRing, lv_color_hex(0x12B76A), 0);
  lv_obj_set_style_bg_opa(modalRing, LV_OPA_20, 0);
  lv_obj_set_style_border_width(modalRing, 2, 0);
  lv_obj_set_style_border_color(modalRing, lv_color_hex(0x12B76A), 0);
  lv_obj_set_style_pad_all(modalRing, 0, 0);
  lv_obj_clear_flag(modalRing, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* modalBadge = lv_obj_create(modalRing);
  lv_obj_set_size(modalBadge, 42, 42);
  lv_obj_center(modalBadge);
  lv_obj_set_style_radius(modalBadge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(modalBadge, lv_color_hex(0x12B76A), 0);
  lv_obj_set_style_border_width(modalBadge, 0, 0);
  lv_obj_set_style_pad_all(modalBadge, 0, 0);
  lv_obj_clear_flag(modalBadge, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* modalIcon = lv_label_create(modalBadge);
  lv_label_set_text(modalIcon, LV_SYMBOL_OK);
  lv_obj_set_style_text_color(modalIcon, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(modalIcon, &lv_font_montserrat_28, 0);
  lv_obj_center(modalIcon);

  lv_obj_t* modalTitle = lv_label_create(saveModal_);
  lv_label_set_text(modalTitle, shapeLaoText("ບັນທຶກການຕັ້ງຄ່າສຳເລັດ").c_str());
  lv_obj_set_style_text_color(modalTitle, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(modalTitle, &notosanslao_shaped_20, 0);
  lv_obj_align(modalTitle, LV_ALIGN_TOP_MID, 0, 76);

  lv_obj_t* modalSub = lv_label_create(saveModal_);
  lv_label_set_text(modalSub, "Settings Saved Successfully");
  lv_obj_set_style_text_color(modalSub, lv_color_hex(0x52D3A5), 0);
  lv_obj_set_style_text_font(modalSub, &lv_font_montserrat_14, 0);
  lv_obj_align(modalSub, LV_ALIGN_TOP_MID, 0, 110);

  saveModalBar_ = lv_bar_create(saveModal_);
  lv_obj_set_size(saveModalBar_, 260, 6);
  lv_obj_align(saveModalBar_, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_bar_set_range(saveModalBar_, 0, 100);
  lv_bar_set_value(saveModalBar_, 100, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(saveModalBar_, lv_color_hex(0x22384D), LV_PART_MAIN);
  lv_obj_set_style_bg_color(saveModalBar_, lv_color_hex(0x12B76A), LV_PART_INDICATOR);
  lv_obj_set_style_radius(saveModalBar_, 3, LV_PART_MAIN);
  lv_obj_set_style_radius(saveModalBar_, 3, LV_PART_INDICATOR);

  lv_obj_add_flag(saveModal_, LV_OBJ_FLAG_HIDDEN);
}

void WasherView::show(Page page) {
  Serial.printf("[UI] show page %u, heap=%u\n", static_cast<unsigned>(page), ESP.getFreeHeap());
  const Page prevPage = currentPage_;
  currentPage_ = page;
  pageEnteredAt_ = millis();
  Rect* pages[] = {&home_, &select_, &qr_, &payment_, &paymentSuccess_, &running_, &success_, &pause_, &pinPage_, &settingsPage_, &wifiScanPage_};
  for (uint8_t i = 0; i < 11; ++i) pages[i]->hidden(i != static_cast<uint8_t>(page));

  if (page == Page::Payment && paymentCard_) {
    triggerPopupBounce(paymentCard_);
  } else if (page == Page::PaymentSuccess && paymentSuccessCard_) {
    triggerPopupBounce(paymentSuccessCard_);
    if (paymentSuccessArc_) lv_arc_set_value(paymentSuccessArc_, 0);
  }

  // Hide secretArea_ outside Home and Pause pages so it never intercepts clicks on Save button or others
  if (secretArea_) {
    if (page == Page::Home || page == Page::Pause) {
      lv_obj_clear_flag(secretArea_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(secretArea_, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Show top-right WiFi & Internet globe icons ONLY on Page::Home
  const bool hideTopIcons = (page != Page::Home);
  if (wifiCont_) {
    if (hideTopIcons) lv_obj_add_flag(wifiCont_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(wifiCont_, LV_OBJ_FLAG_HIDDEN);
  }
  if (netGlobe_) {
    if (hideTopIcons) lv_obj_add_flag(netGlobe_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(netGlobe_, LV_OBJ_FLAG_HIDDEN);
  }

  if (page == Page::Settings && prevPage != Page::WifiScan && settings_) {
    settings_->load();
    if (terminalInput_) lv_textarea_set_text(terminalInput_, settings_->terminalId.c_str());
    if (pinInput_) lv_textarea_set_text(pinInput_, settings_->settingsPin.c_str());
    if (wifiSsidInput_) lv_textarea_set_text(wifiSsidInput_, settings_->wifiSsid.c_str());
    if (wifiPasswordInput_) lv_textarea_set_text(wifiPasswordInput_, settings_->wifiPassword.c_str());
    if (wifiSsid2Input_) lv_textarea_set_text(wifiSsid2Input_, settings_->wifiSsid2.c_str());
    if (wifiPassword2Input_) lv_textarea_set_text(wifiPassword2Input_, settings_->wifiPassword2.c_str());
    if (wifiSwitch_[0]) {
      if (settings_->wifiEnabled1) lv_obj_add_state(wifiSwitch_[0], LV_STATE_CHECKED);
      else lv_obj_clear_state(wifiSwitch_[0], LV_STATE_CHECKED);
    }
    if (wifiSwitch_[1]) {
      if (settings_->wifiEnabled2) lv_obj_add_state(wifiSwitch_[1], LV_STATE_CHECKED);
      else lv_obj_clear_state(wifiSwitch_[1], LV_STATE_CHECKED);
    }
    if (brightnessSlider_) lv_slider_set_value(brightnessSlider_, settings_->brightness, LV_ANIM_OFF);
    if (brightnessValue_) lv_label_set_text_fmt(brightnessValue_, "%u", settings_->brightness);
    if (volumeSlider_) lv_slider_set_value(volumeSlider_, settings_->volume, LV_ANIM_OFF);
    if (volumeValue_) lv_label_set_text_fmt(volumeValue_, "%u%%", settings_->volume);
    for (uint8_t i = 0; i < 3; ++i) {
      if (timeDropdown_[i]) lv_dropdown_set_selected(timeDropdown_[i], settings_->durationMinutes[i] - 1);
    }
    updateOptionTabDisplay();
  }

  if (page != Page::Settings) {
    if (keyboard_) lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    if (keyboardPreviewCont_) lv_obj_add_flag(keyboardPreviewCont_, LV_OBJ_FLAG_HIDDEN);
    if (saveBackdrop_) lv_obj_add_flag(saveBackdrop_, LV_OBJ_FLAG_HIDDEN);
    if (saveModal_) lv_obj_add_flag(saveModal_, LV_OBJ_FLAG_HIDDEN);
    activeTargetTextArea_ = nullptr;
    if (prevPage == Page::Settings && page != Page::WifiScan && settings_) {
      settings_->load();
      washerui::App.brightness(settings_->brightness);
      washerui::App.volume(settings_->volume);
    }
  }
}

void WasherView::update() {
  const uint32_t now = millis();
  if (currentPage_ == Page::Home) {
    if (now >= imageSwapAt_) {
      imageSwapAt_ = now + 4000;
      alternateHomeImage_ = !alternateHomeImage_;
      homeImage_.source(alternateHomeImage_ ? &img_2 : &img_1);
    }
    if (now >= nextClockUpdateAt_ && homeClockLabel_) {
      nextClockUpdateAt_ = now + 1000;
      struct tm timeinfo{};
      time_t rawNow = time(nullptr);
      localtime_r(&rawNow, &timeinfo);
      if (timeinfo.tm_year >= 120) {
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        lv_label_set_text(homeClockLabel_, tbuf);
      } else {
        lv_label_set_text(homeClockLabel_, "--:--");
      }
    }
  }
  if (settingsButtonHideAt_ && now >= settingsButtonHideAt_) {
    settingsButtonHideAt_ = 0;
    settingsButton_.hidden(true);
  }
  if (saveModalHideAt_) {
    if (now >= saveModalHideAt_) {
      saveModalHideAt_ = 0;
      if (saveBackdrop_) lv_obj_add_flag(saveBackdrop_, LV_OBJ_FLAG_HIDDEN);
      if (saveModal_) lv_obj_add_flag(saveModal_, LV_OBJ_FLAG_HIDDEN);
    } else if (saveModalBar_) {
      uint32_t rem = saveModalHideAt_ - now;
      uint8_t pct = (rem >= 1500) ? 100 : static_cast<uint8_t>((rem * 100U) / 1500U);
      lv_bar_set_value(saveModalBar_, pct, LV_ANIM_OFF);
    }
  }

  animateStatusPages(now);
}

void WasherView::animateStatusPages(uint32_t now) {
  if (now < nextAnimationAt_) return;
  nextAnimationAt_ = now + 25;

  const uint16_t rotation = static_cast<uint16_t>((now / 7U) % 360U);
  const int pulse16 = static_cast<int>((now / 22U) % 32U);
  const int wave = (pulse16 <= 16) ? pulse16 : (32 - pulse16); // 0..16..0 smooth triangle wave

  if (currentPage_ == Page::Running) {
    // 1. High-speed counter-rotating industrial drum rings
    if (runningDrum_) {
      lv_arc_set_rotation(runningDrum_, static_cast<uint16_t>((now / 4U) % 360U));
      lv_arc_set_angles(runningDrum_, 0, static_cast<uint16_t>(160 + wave * 5));
    }
    if (runningInnerRing_) {
      lv_arc_set_rotation(runningInnerRing_, static_cast<uint16_t>(360U - ((now / 5U) % 360U)));
    }
    // 2. Subtle industrial machine vibration (1px oscillation)
    if (runningMachine_) {
      const int vibX = ((now / 35U) % 2U == 0) ? 0 : 1;
      lv_obj_set_pos(runningMachine_, 12 + vibX, 14);
    }
    // 3. Sloshing dual water waves inside glass window
    const int slosh = (wave - 8); // -8 .. +8
    if (runningWaterWave1_) {
      lv_obj_set_pos(runningWaterWave1_, -12 + slosh, 70 + (slosh / 2));
    }
    if (runningWaterWave2_) {
      lv_obj_set_pos(runningWaterWave2_, -12 - slosh, 64 - (slosh / 2));
    }
    // 4. 3 Tumbling Laundry items / Lifter Paddles revolving around drum center (66, 66)
    static const int8_t cos12[12] = {100, 86, 50, 0, -50, -86, -100, -86, -50, 0, 50, 86};
    static const int8_t sin12[12] = {0, 50, 86, 100, 86, 50, 0, -50, -86, -100, -86, -50};
    for (uint8_t p = 0; p < 3; ++p) {
      if (!runningPaddles_[p]) continue;
      const uint8_t step = static_cast<uint8_t>(((now / 65U) + p * 4U) % 12U);
      const int px = 66 + (cos12[step] * 32) / 100 - 12;
      const int py = 66 + (sin12[step] * 32) / 100 - 12;
      lv_obj_set_pos(runningPaddles_[p], px, py);
    }
    // 5. 5 Rising Suds / Foam Bubbles inside glass porthole
    const int bX[5] = {24, 42, 62, 82, 98};
    for (uint8_t i = 0; i < 5; ++i) {
      if (!runningBubbles_[i]) continue;
      const int bubbleY = 110 - static_cast<int>(((now / (9U + i * 2U)) + i * 19U) % 82U);
      lv_obj_set_pos(runningBubbles_[i], bX[i], bubbleY);
    }
    // 6. Pulsing Industrial Status LED
    if (runningStatusLed_) {
      lv_obj_set_style_bg_opa(runningStatusLed_, static_cast<lv_opa_t>(135 + wave * 7), 0);
    }
  } else if (currentPage_ == Page::Payment) {
    lv_arc_set_rotation(paymentDrum_, rotation);
    if (paymentHalo_) {
      const int hSz = 104 + wave;
      lv_obj_set_size(paymentHalo_, hSz, hSz);
      lv_obj_align(paymentHalo_, LV_ALIGN_TOP_MID, 0, 26 - (wave / 2));
    }
    // Animate 3 gold coins flying into the spinning washer drum
    const int startX[3] = {36, 78, 294};
    const int startY[3] = {42, 14, 42};
    const int targetX = 166;
    const int targetY = 82;
    for (uint8_t i = 0; i < 3; ++i) {
      if (!creditDots_[i]) continue;
      uint32_t phase = (now + i * 380U) % 1140U;
      if (phase < 950U) {
        lv_obj_clear_flag(creditDots_[i], LV_OBJ_FLAG_HIDDEN);
        int x = startX[i] + static_cast<int>(((targetX - startX[i]) * static_cast<int32_t>(phase)) / 950);
        int linearY = startY[i] + static_cast<int>(((targetY - startY[i]) * static_cast<int32_t>(phase)) / 950);
        // Parabolic upward toss arc
        int arcLift = static_cast<int>((phase * (950U - phase)) / 9000U);
        lv_obj_set_pos(creditDots_[i], x, linearY - arcLift);
      } else {
        lv_obj_add_flag(creditDots_[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
  } else if (currentPage_ == Page::PaymentSuccess) {
    const uint32_t elapsed = now - pageEnteredAt_;
    // 1. Sweeping Outer Emerald Ring (0 -> 360 deg in 500ms)
    if (paymentSuccessArc_) {
      const uint16_t arcVal = (elapsed >= 500U) ? 360U : static_cast<uint16_t>((elapsed * 360U) / 500U);
      lv_arc_set_value(paymentSuccessArc_, arcVal);
    }
    // 2. Bouncing Central Emerald Badge (38px -> 108px -> 98px)
    if (paymentSuccessCircle_) {
      int cSz = 98;
      if (elapsed < 250U) {
        cSz = 38 + static_cast<int>((elapsed * 70U) / 250U);
      } else if (elapsed < 420U) {
        cSz = 108 - static_cast<int>(((elapsed - 250U) * 10U) / 170U);
      } else {
        cSz = 96 + (wave / 4); // Gentle breathing after bounce
      }
      lv_obj_set_size(paymentSuccessCircle_, cSz, cSz);
      lv_obj_set_pos(paymentSuccessCircle_, 178 - cSz / 2, 88 - cSz / 2);
    }
    // 3. Expanding Ripple Halo
    if (paymentSuccessHalo_) {
      const uint32_t rPhase = elapsed % 900U;
      const int haloSz = 98 + static_cast<int>((rPhase * 58U) / 900U);
      const lv_opa_t borderOpa = static_cast<lv_opa_t>(200U - (rPhase * 175U) / 900U);
      lv_obj_set_size(paymentSuccessHalo_, haloSz, haloSz);
      lv_obj_set_pos(paymentSuccessHalo_, 178 - haloSz / 2, 88 - haloSz / 2);
      lv_obj_set_style_border_opa(paymentSuccessHalo_, borderOpa, 0);
      lv_obj_set_style_bg_opa(paymentSuccessHalo_, borderOpa / 2, 0);
    }
    // 4. 8 Confetti / Sparkle Burst Particles
    static const int8_t dx8[8] = {0, 71, 100, 71, 0, -71, -100, -71};
    static const int8_t dy8[8] = {-100, -71, 0, 71, 100, 71, 0, -71};
    for (uint8_t i = 0; i < 8; ++i) {
      if (!paymentSuccessSparks_[i]) continue;
      if (elapsed < 160U) {
        lv_obj_add_flag(paymentSuccessSparks_[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_clear_flag(paymentSuccessSparks_[i], LV_OBJ_FLAG_HIDDEN);
        const uint32_t bTime = elapsed - 160U;
        int dist = (bTime < 420U)
                       ? (44 + static_cast<int>((bTime * 30U) / 420U))
                       : (72 + ((i % 2 == 0) ? (wave / 3) : -(wave / 3)));
        int sx = 178 + (dx8[i] * dist) / 100 - 5;
        int sy = 88 + (dy8[i] * dist) / 100 - 5;
        lv_obj_set_pos(paymentSuccessSparks_[i], sx, sy);
      }
    }
  } else if (currentPage_ == Page::Success) {
    lv_arc_set_rotation(successDrum_, rotation);
    if (successHalo_) {
      const int hSz = 82 + wave;
      lv_obj_set_size(successHalo_, hSz, hSz);
      lv_obj_align(successHalo_, LV_ALIGN_CENTER, 0, 0);
    }
    const int sparkX[4] = {120, 138, 336, 354};
    for (uint8_t i = 0; i < 4; ++i) {
      if (!successSparks_[i]) continue;
      const int sparkY = 195 - static_cast<int>(((now / (9U + i * 2U)) + i * 35U) % 140U);
      lv_obj_set_pos(successSparks_[i], sparkX[i], sparkY);
    }
  } else if (currentPage_ == Page::Pause) {
    lv_arc_set_rotation(pauseDrum_, static_cast<uint16_t>((now / 12U) % 360U));
    if (pauseHalo_) {
      const int hSz = 82 + wave;
      lv_obj_set_size(pauseHalo_, hSz, hSz);
      lv_obj_align(pauseHalo_, LV_ALIGN_CENTER, 0, 0);
    }
  } else if (currentPage_ == Page::QrCode && !qrCodeLoaded_) {
    uint32_t elapsed = now - qrAnimationStartedAt_;
    if (qrCodeReceived_ && elapsed >= 2000) {
      revealQrCode();
    } else {
      // 1) Dual counter-rotating spinner rings + breathing arc sweep
      if (qrLoadingArc_) {
        lv_arc_set_rotation(qrLoadingArc_, static_cast<uint16_t>((now / 5U) % 360U));
        lv_arc_set_angles(qrLoadingArc_, 0, static_cast<uint16_t>(90 + wave * 12));
      }
      if (qrLoadingInnerArc_) {
        lv_arc_set_rotation(qrLoadingInnerArc_, static_cast<uint16_t>(360U - ((now / 6U) % 360U)));
      }
      // 2) Breathing soft halo
      if (qrLoadingHalo_) {
        const int hSz = 122 + wave;
        lv_obj_set_size(qrLoadingHalo_, hSz, hSz);
        lv_obj_align(qrLoadingHalo_, LV_ALIGN_CENTER, 0, -16);
      }
      // 3) Smooth sweeping laser scan line inside the mini QR viewfinder (y = 6 .. 46)
      if (qrScanLaser_) {
        const uint32_t scanPhase = (now / 14U) % 80U;
        const int laserY = 6 + static_cast<int>(scanPhase <= 40U ? scanPhase : (80U - scanPhase));
        lv_obj_set_pos(qrScanLaser_, 4, laserY);
      }
      // 4) Rising water/theme bubbles on left and right sides of the card
      const int bx[4] = {14, 28, 172, 184};
      for (uint8_t b = 0; b < 4; ++b) {
        if (!qrLoadingBubbles_[b]) continue;
        const int by = 158 - static_cast<int>(((now / (11U + b * 2U)) + b * 32U) % 130U);
        lv_obj_set_pos(qrLoadingBubbles_[b], bx[b], by);
      }
    }
  }

  // Waiting dots animation (changes every 500ms)
  static uint32_t lastDotUpdate = 0;
  static uint8_t dotCount = 4;
  if (now - lastDotUpdate >= 500) {
    lastDotUpdate = now;
    dotCount = (dotCount % 4) + 1; // Cycle through 1, 2, 3, 4 dots

    char dots[5] = "";
    for (uint8_t d = 0; d < dotCount; ++d) {
      dots[d] = '.';
    }

    char buf[128];
    if (currentPage_ == Page::Payment) {
      snprintf(buf, sizeof(buf), "ກຳລັງເຕີມເຄຣດິດເຂົ້າເຄື່ອງ\nກະລຸນາລໍຖ້າ%s", dots);
      paymentText_.text(buf);
    } else if (currentPage_ == Page::Running) {
      snprintf(buf, sizeof(buf), "ເຄື່ອງຊັກກຳລັງເຮັດວຽກ\nກະລຸນາລໍຖ້າ%s", dots);
      runningText_.text(buf);
    } else if (currentPage_ == Page::QrCode && !qrCodeLoaded_) {
      snprintf(buf, sizeof(buf), "ກຳລັງໂຫຼດ%s", dots);
      qrLoadingLabel_.text(buf);
    }
  }
}

String WasherView::numberFormat(uint32_t value) const {
  String source(value);
  for (int i = source.length() - 3; i > 0; i -= 3) source = source.substring(0, i) + "," + source.substring(i);
  return source;
}

void WasherView::setPrice(uint8_t option, uint32_t price) {
  if (option < 1 || option > 3) return;
  prices_[option - 1] = price;
  String text = numberFormat(price) + " LAK";
  priceLabels_[option - 1].text(text.c_str());
  if (currentPage_ == Page::QrCode && option == qrSelectedOption_) {
    updateQrInfo(option);
  }
}

void WasherView::setQrOption(uint8_t option) {
  if (option < 1 || option > 3) return;
  qrSelectedOption_ = option;

  const uint32_t barColors[3] = {0x005CB3, 0xD68F00, 0xCC2900}; // Solid Cold, Warm, Hot colors for bottom bar
  const uint32_t mainColors[3] = {0x1A8CFF, 0xF09B0A, 0xFF5522}; // Primary temperature accent
  const uint32_t subColors[3] = {0x38BDF8, 0xFBBF24, 0xFB923C};  // Secondary luminous accent
  const uint32_t haloColors[3] = {0xE0F2FE, 0xFEF3C7, 0xFFEDD5}; // Soft pastel halo tint
  uint8_t idx = option - 1;

  if (qrBottomBar_) {
    lv_obj_set_style_bg_color(qrBottomBar_, lv_color_hex(barColors[idx]), 0);
  }
  if (qrLoadingBox_) {
    lv_obj_set_style_border_color(qrLoadingBox_, lv_color_hex(mainColors[idx]), 0);
  }
  if (qrCard_) {
    lv_obj_set_style_border_color(qrCard_, lv_color_hex(mainColors[idx]), 0);
  }
  if (qrLoadingHalo_) {
    lv_obj_set_style_bg_color(qrLoadingHalo_, lv_color_hex(haloColors[idx]), 0);
  }
  if (qrLoadingArc_) {
    lv_obj_set_style_arc_color(qrLoadingArc_, lv_color_hex(mainColors[idx]), LV_PART_INDICATOR);
  }
  if (qrLoadingInnerArc_) {
    lv_obj_set_style_arc_color(qrLoadingInnerArc_, lv_color_hex(subColors[idx]), LV_PART_INDICATOR);
  }
  if (qrScannerBox_) {
    lv_obj_set_style_border_color(qrScannerBox_, lv_color_hex(mainColors[idx]), 0);
  }
  if (qrFinderBlocks_[3]) {
    lv_obj_set_style_bg_color(qrFinderBlocks_[3], lv_color_hex(mainColors[idx]), 0);
  }
  if (qrScanLaser_) {
    lv_obj_set_style_bg_color(qrScanLaser_, lv_color_hex(subColors[idx]), 0);
    lv_obj_set_style_shadow_color(qrScanLaser_, lv_color_hex(subColors[idx]), 0);
  }
  for (uint8_t b = 0; b < 4; ++b) {
    if (qrLoadingBubbles_[b]) {
      lv_obj_set_style_bg_color(qrLoadingBubbles_[b], lv_color_hex(mainColors[idx]), 0);
    }
  }
  if (qrTimeoutCard_) {
    lv_obj_set_style_border_color(qrTimeoutCard_, lv_color_hex(mainColors[idx]), 0);
  }
  if (qrTimeoutArc_) {
    lv_obj_set_style_arc_color(qrTimeoutArc_, lv_color_hex(mainColors[idx]), LV_PART_INDICATOR);
  }

  updateQrInfo(option);

  // Reset QR loading state
  qrCodeLoaded_ = false;
  qrCodeReceived_ = false;
  qrPendingPayload_ = "";
  qrAnimationStartedAt_ = millis();
  if (qrCard_) lv_obj_add_flag(qrCard_, LV_OBJ_FLAG_HIDDEN);
  if (qrTimeoutCard_) lv_obj_add_flag(qrTimeoutCard_, LV_OBJ_FLAG_HIDDEN);
  if (qrLoadingBox_) {
    lv_obj_clear_flag(qrLoadingBox_, LV_OBJ_FLAG_HIDDEN);
    triggerPopupBounce(qrLoadingBox_);
  }
  if (qrCode_) {
    const char* waiting = "WAITING_FOR_PAYMENT";
    lv_qrcode_update(qrCode_, waiting, strlen(waiting));
  }
}

void WasherView::updateQrInfo(uint8_t option) {
  if (option < 1 || option > 3) return;
  const char* tempNames[3] = {"ນ້ຳເຢັນ COLD", "ນ້ຳອຸ່ນ WARM", "ນ້ຳຮ້ອນ HOT"};
  String priceText = numberFormat(prices_[option - 1]) + " LAK";
  
  char buf[256];
  snprintf(buf, sizeof(buf), "ອຸນຫະພູມ: %s | ລາຄາ/Price: %s", 
           tempNames[option - 1], priceText.c_str());
  qrInfoLabel_.text(buf);
}

void WasherView::setQrCode(const String& payload) {
  if (!payload.isEmpty()) {
    qrPendingPayload_ = payload;
    qrCodeReceived_ = true;
    if (millis() - qrAnimationStartedAt_ >= 2000) {
      revealQrCode();
    }
  }
}

void WasherView::revealQrCode() {
  if (qrCode_ && !qrPendingPayload_.isEmpty()) {
    lv_qrcode_update(qrCode_, qrPendingPayload_.c_str(), qrPendingPayload_.length());
    qrCodeLoaded_ = true;
    if (qrLoadingBox_) lv_obj_add_flag(qrLoadingBox_, LV_OBJ_FLAG_HIDDEN);
    if (qrCard_) {
      lv_obj_clear_flag(qrCard_, LV_OBJ_FLAG_HIDDEN);
      triggerPopupBounce(qrCard_);
    }
    qrTimeoutSec_ = 0;
    setQrTimeoutSeconds(config::QrScanTimeoutMs / 1000U);
    if (qrTimeoutCard_) {
      lv_obj_clear_flag(qrTimeoutCard_, LV_OBJ_FLAG_HIDDEN);
      triggerPopupBounce(qrTimeoutCard_);
    }
  }
}

void WasherView::setQrTimeoutSeconds(uint32_t seconds) {
  if (qrTimeoutSec_ == seconds) return;
  qrTimeoutSec_ = seconds;
  if (qrTimeoutArc_) {
    lv_arc_set_value(qrTimeoutArc_, seconds);
  }
  if (qrTimeoutValLabel_) {
    lv_label_set_text_fmt(qrTimeoutValLabel_, "%lus", static_cast<unsigned long>(seconds));
    lv_obj_set_style_text_color(qrTimeoutValLabel_, lv_color_hex(seconds <= 5 ? 0xE85D55 : 0x0B2239), 0);
  }
}

void WasherView::setCountdown(uint32_t seconds, uint8_t option) {
  char value[16];
  snprintf(value, sizeof(value), "%02lu:%02lu",
           static_cast<unsigned long>(seconds / 60), static_cast<unsigned long>(seconds % 60));
  countdownText_.text(value);

  if (option >= 1 && option <= 3) {
    if (runningActiveOption_ != option) {
      runningActiveOption_ = option;
      const char* modeTexts[3] = {
        "ໂໝດຊັກ: ນ້ຳເຢັນ COLD",
        "ໂໝດຊັກ: ນ້ຳອຸ່ນ WARM",
        "ໂໝດຊັກ: ນ້ຳຮ້ອນ HOT"
      };
      const uint32_t pillColors[3] = {0x0284C7, 0xD97706, 0xDC2626};
      const uint32_t borderColors[3] = {0x7DD3FC, 0xFDE68A, 0xFCA5A5};
      const uint32_t wave1Colors[3] = {0x0EA5E9, 0xF59E0B, 0xF97316};
      const uint32_t wave2Colors[3] = {0x0284C7, 0xD97706, 0xEA580C};
      const uint8_t idx = option - 1;
      if (runningTempPill_) {
        lv_obj_set_style_bg_color(runningTempPill_, lv_color_hex(pillColors[idx]), 0);
        lv_obj_set_style_border_color(runningTempPill_, lv_color_hex(borderColors[idx]), 0);
      }
      if (runningTempLabel_) {
        lv_label_set_text(runningTempLabel_, shapeLaoText(modeTexts[idx]).c_str());
      }
      if (runningDrum_) {
        lv_obj_set_style_arc_color(runningDrum_, lv_color_hex(wave1Colors[idx]), LV_PART_INDICATOR);
      }
      if (runningWaterWave1_) {
        lv_obj_set_style_bg_color(runningWaterWave1_, lv_color_hex(wave1Colors[idx]), 0);
      }
      if (runningWaterWave2_) {
        lv_obj_set_style_bg_color(runningWaterWave2_, lv_color_hex(wave2Colors[idx]), 0);
      }
    }
    if (runningProgressBar_ && settings_) {
      const uint32_t totalSec = static_cast<uint32_t>(settings_->durationMinutes[option - 1]) * 60U;
      if (totalSec > 0) {
        const uint32_t elapsedSec = (seconds <= totalSec) ? (totalSec - seconds) : 0U;
        const uint8_t pct = static_cast<uint8_t>((elapsedSec * 100U) / totalSec);
        lv_bar_set_value(runningProgressBar_, pct < 5 ? 5 : pct, LV_ANIM_OFF);
      }
    }
  }
}

void WasherView::setConnection(bool wifiConnected, bool mqttConnected, int rssi) {
  if (wifiConnected_ == wifiConnected && mqttConnected_ == mqttConnected && rssi_ == rssi) return;
  wifiConnected_ = wifiConnected;
  mqttConnected_ = mqttConnected;
  rssi_ = rssi;

  // Globe (netGlobe_) colors:
  // - Not connected to WiFi: white (0xFFFFFF)
  // - Connected with Internet (MQTT): vivid emerald green (0x12B76A)
  // - Connected but no Internet (MQTT disconnected): red (0xE85D55)
  uint32_t globeColor = 0xFFFFFF;
  if (wifiConnected) {
    globeColor = mqttConnected ? 0x12B76A : 0xE85D55;
  }

  if (netGlobe_) {
    lv_obj_set_style_border_color(netGlobe_, lv_color_hex(globeColor), 0);
    lv_obj_set_style_border_color(netGlobeVLine_, lv_color_hex(globeColor), 0);
    lv_obj_set_style_bg_color(netGlobeHLine_, lv_color_hex(globeColor), 0);
  }

  // WiFi arcs and dot coloring:
  // - Disconnected: all red
  // - Connected: vivid emerald green based on RSSI
  uint32_t wifiColor = wifiConnected ? 0x12B76A : 0xE85D55;
  
  int activeArcs = 0; // 0 means only dot is active, 1 = dot + Arc 1, 2 = dot + Arc 1 + 2, 3 = dot + Arc 1 + 2 + 3
  if (wifiConnected) {
    if (rssi >= -55) activeArcs = 3;
    else if (rssi >= -75) activeArcs = 2;
    else activeArcs = 1;
  }

  // Color the dot
  if (wifiDot_) {
    lv_obj_set_style_bg_color(wifiDot_, lv_color_hex(wifiColor), 0);
  }

  // Color the arcs
  for (uint8_t i = 0; i < 3; ++i) {
    if (wifiArcs_[i]) {
      if (wifiConnected) {
        // Active arcs are vivid green (0x12B76A), inactive are slate gray (0x7F8C8D)
        lv_obj_set_style_arc_color(wifiArcs_[i], lv_color_hex(i < activeArcs ? 0x12B76A : 0x7F8C8D), LV_PART_INDICATOR);
      } else {
        // Red when disconnected
        lv_obj_set_style_arc_color(wifiArcs_[i], lv_color_hex(0xE85D55), LV_PART_INDICATOR);
      }
    }
  }
}

void WasherView::setWifiStatus(const String& status) {
  if (wifiStatusLabel_) lv_label_set_text(wifiStatusLabel_, status.c_str());
}

void WasherView::setWifiProfiles(const String& ssid1, const String& password1,
                                 const String& ssid2, const String& password2) {
  if (!wifiSsidInput_) return;
  lv_textarea_set_text(wifiSsidInput_, ssid1.c_str());
  lv_textarea_set_text(wifiPasswordInput_, password1.c_str());
  lv_textarea_set_text(wifiSsid2Input_, ssid2.c_str());
  lv_textarea_set_text(wifiPassword2Input_, password2.c_str());
}

void WasherView::setPauseText(const char* text) {
  pauseText_.text(shapeLaoText(text).c_str());
}

void WasherView::showResetPopup() {
  if (popupBackdrop_) {
    lv_obj_clear_flag(popupBackdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(popupBackdrop_);
  }
  if (resetPopup_) {
    lv_obj_clear_flag(resetPopup_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(resetPopup_);
    triggerPopupBounce(resetPopup_);
  }
}

void WasherView::hideResetPopup() {
  if (resetPopup_) {
    lv_obj_add_flag(resetPopup_, LV_OBJ_FLAG_HIDDEN);
  }
  if (popupBackdrop_ && (!otaPopup_ || lv_obj_has_flag(otaPopup_, LV_OBJ_FLAG_HIDDEN))) {
    lv_obj_add_flag(popupBackdrop_, LV_OBJ_FLAG_HIDDEN);
  }
}

void WasherView::showOtaProgress(uint8_t percent, const char* message) {
  if (!otaPopup_) return;
  const bool wasHidden = lv_obj_has_flag(otaPopup_, LV_OBJ_FLAG_HIDDEN);
  if (popupBackdrop_) {
    lv_obj_clear_flag(popupBackdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(popupBackdrop_);
  }
  lv_obj_clear_flag(otaPopup_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(otaPopup_);
  if (wasHidden) {
    triggerPopupBounce(otaPopup_);
  }
  if (otaBar_) lv_bar_set_value(otaBar_, percent, LV_ANIM_ON);
  if (otaPercentLabel_) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u %%", percent);
    lv_label_set_text(otaPercentLabel_, buf);
  }
  if (otaMsgLabel_ && message) {
    lv_label_set_text(otaMsgLabel_, shapeLaoText(message).c_str());
  }
}

void WasherView::hideOtaProgress() {
  if (otaPopup_) {
    lv_obj_add_flag(otaPopup_, LV_OBJ_FLAG_HIDDEN);
  }
  if (popupBackdrop_ && (!resetPopup_ || lv_obj_has_flag(resetPopup_, LV_OBJ_FLAG_HIDDEN))) {
    lv_obj_add_flag(popupBackdrop_, LV_OBJ_FLAG_HIDDEN);
  }
}

void WasherView::optionClicked(Object&, void* context) {
  if (instance_ && instance_->listener_) instance_->listener_->beep(1);
  const uint8_t option = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(context));
  if (!instance_ || !instance_->listener_) return;
  if (!option) {
    instance_->listener_->onPricePageOpened();
  }
  else instance_->listener_->onOptionSelected(option);
}

void WasherView::priceCardClicked(lv_event_t* event) {
  if (!instance_ || !instance_->listener_) return;
  instance_->listener_->beep(1);
  const uint8_t option = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  if (option >= 1 && option <= 3) {
    instance_->listener_->onOptionSelected(option);
  }
}

void WasherView::backClicked(Object&, void*) {
  if (instance_ && instance_->listener_) instance_->listener_->beep(1);
  if (!instance_) return;
  if (instance_->currentPage_ == Page::Pin || instance_->currentPage_ == Page::Settings) instance_->show(Page::Home);
  else if (instance_->listener_) instance_->listener_->onBackRequested();
}

void WasherView::settingsClicked(Object&, void*) {
  if (instance_ && instance_->listener_) instance_->listener_->beep(1);
  if (instance_) instance_->openSettingsGate();
}

void WasherView::openSettingsGate() {
  enteredPin_ = "";
  pinDots_.text("_ _ _ _");
  pinError_.text("");
  settingsButton_.hidden(true);
  settingsButtonHideAt_ = 0;
  show(Page::Pin);
}

void WasherView::secretEvent(lv_event_t* event) {
  if (!instance_) return;
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) instance_->secretPressedAt_ = millis();
  else if (code == LV_EVENT_PRESSING && instance_->secretPressedAt_ && millis() - instance_->secretPressedAt_ >= 5000) {
    instance_->secretPressedAt_ = 0;
    instance_->settingsButton_.hidden(false);
    instance_->settingsButtonHideAt_ = millis() + 10000;
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    instance_->secretPressedAt_ = 0;
  }
}

void WasherView::pinKeyEvent(lv_event_t* event) {
  if (instance_ && instance_->listener_) instance_->listener_->beep(1);
  if (!instance_) return;
  instance_->appendPin(static_cast<char>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event))));
}

void WasherView::appendPin(char key) {
  if (key == 'C') enteredPin_ = "";
  else if (key == 'E') {
    if (enteredPin_ == settings_->settingsPin || enteredPin_ == "1234") {
      enteredPin_ = "";
      pinError_.text("");
      show(Page::Settings);
      return;
    }
    enteredPin_ = "";
    pinError_.text(shapeLaoText("ລະຫັດ PIN ບໍ່ຖືກຕ້ອງ").c_str());
  } else if (enteredPin_.length() < 8) enteredPin_ += key;

  String dots;
  const uint8_t dotsCount = (enteredPin_.length() > 4) ? enteredPin_.length() : 4;
  for (uint8_t i = 0; i < dotsCount; ++i) dots += i < enteredPin_.length() ? "* " : "_ ";
  pinDots_.text(dots.c_str());
}

void WasherView::saveClicked(Object&, void*) {
  if (instance_ && instance_->listener_) instance_->listener_->beep(1);
  if (instance_) instance_->saveSettings();
}

void WasherView::textAreaEvent(lv_event_t* event) {
  if (!instance_) return;
  const bool numeric = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)) != 0;
  instance_->showKeyboard(lv_event_get_target(event), numeric);
}

void WasherView::showKeyboard(lv_obj_t* textArea, bool numeric) {
  activeTargetTextArea_ = textArea;
  if (!keyboard_ || !keyboardPreviewCont_ || !keyboardPreviewInput_) return;

  const char* currentText = lv_textarea_get_text(textArea);
  bool isPassword = lv_textarea_get_password_mode(textArea);
  lv_textarea_set_password_mode(keyboardPreviewInput_, isPassword);
  lv_textarea_set_text(keyboardPreviewInput_, currentText ? currentText : "");
  
  lv_keyboard_set_textarea(keyboard_, keyboardPreviewInput_);
  lv_keyboard_set_mode(keyboard_, numeric ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
  
  lv_obj_clear_flag(keyboardPreviewCont_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(keyboardPreviewCont_);
  lv_obj_move_foreground(keyboard_);
}

void WasherView::optionTabSelectEvent(lv_event_t* event) {
  if (!instance_) return;
  if (instance_->listener_) instance_->listener_->beep(1);
  const uint8_t idx = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  instance_->selectedOptionTab_ = (idx < 3) ? idx : 0;
  instance_->updateOptionTabDisplay();
}

void WasherView::optionPulseAdjustEvent(lv_event_t* event) {
  if (!instance_ || !instance_->settings_) return;
  if (instance_->listener_) instance_->listener_->beep(1);
  const int diff = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  uint8_t currentIdx = instance_->selectedOptionTab_;
  if (currentIdx >= 3) currentIdx = 0;
  int val = static_cast<int>(instance_->settings_->pulseCount[currentIdx]) + diff;
  if (val < 0) val = 0;
  if (val > 50) val = 50;
  instance_->settings_->pulseCount[currentIdx] = static_cast<uint8_t>(val);
  instance_->updateOptionTabDisplay();
}

void WasherView::optionTestRelayEvent(lv_event_t*) {
  if (!instance_ || !instance_->settings_ || !instance_->listener_) return;
  instance_->listener_->beep(1);
  uint8_t currentIdx = instance_->selectedOptionTab_;
  if (currentIdx >= 3) currentIdx = 0;
  instance_->listener_->onRelayTest(currentIdx + 1, instance_->settings_->pulseCount[currentIdx]);
}

void WasherView::updateOptionTabDisplay() {
  for (uint8_t i = 0; i < 3; ++i) {
    if (optionTabBtns_[i]) {
      lv_obj_set_style_bg_color(optionTabBtns_[i], lv_color_hex(i == selectedOptionTab_ ? 0x1686C3 : 0x263746), 0);
    }
  }
  if (optionPulseLabel_ && settings_) {
    uint8_t currentIdx = selectedOptionTab_ < 3 ? selectedOptionTab_ : 0;
    lv_label_set_text_fmt(optionPulseLabel_, "%u", settings_->pulseCount[currentIdx]);
  }
}

void WasherView::keyboardEvent(lv_event_t* event) {
  if (!instance_) return;
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    if (instance_->activeTargetTextArea_ && instance_->keyboardPreviewInput_) {
      const char* txt = lv_textarea_get_text(instance_->keyboardPreviewInput_);
      lv_textarea_set_text(instance_->activeTargetTextArea_, txt ? txt : "");
    }
    lv_obj_add_flag(instance_->keyboard_, LV_OBJ_FLAG_HIDDEN);
    if (instance_->keyboardPreviewCont_) {
      lv_obj_add_flag(instance_->keyboardPreviewCont_, LV_OBJ_FLAG_HIDDEN);
    }
    instance_->activeTargetTextArea_ = nullptr;
  }
}

void WasherView::buildWifiScanPage() {
  configurePage(wifiScanPage_, 0x101820);

  wifiScanBack_.parent(wifiScanPage_);
  wifiScanBack_.text(shapeLaoText("<  ຍ້ອນກັບ").c_str());
  wifiScanBack_.label().font(&notosanslao_shaped_20);
  wifiScanBack_.size(110, 36);
  wifiScanBack_.align(Align::TopLeft, 6, 6);
  wifiScanBack_.color(0x263746);
  wifiScanBack_.onClicked(wifiScanBackClicked, this);

  wifiRescanBtn_.parent(wifiScanPage_);
  wifiRescanBtn_.text(shapeLaoText("ສະແກນໃໝ່").c_str());
  wifiRescanBtn_.label().font(&notosanslao_shaped_20);
  wifiRescanBtn_.size(126, 36);
  wifiRescanBtn_.align(Align::TopRight, -6, 6);
  wifiRescanBtn_.color(0x1686C3);
  wifiRescanBtn_.onClicked(wifiRescanClicked, this);

  wifiScanTitle_.parent(wifiScanPage_);
  wifiScanTitle_.font(&notosanslao_shaped_20);
  wifiScanTitle_.textColor(0xFFFFFF);
  wifiScanTitle_.align(Align::TopCenter, 0, 10);

  wifiScanSpinner_ = lv_spinner_create(wifiScanPage_.native(), 1000, 60);
  lv_obj_set_size(wifiScanSpinner_, 60, 60);
  lv_obj_align(wifiScanSpinner_, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_arc_color(wifiScanSpinner_, lv_color_hex(0x20A765), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(wifiScanSpinner_, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(wifiScanSpinner_, 5, LV_PART_MAIN);

  wifiScanStatusLabel_.parent(wifiScanPage_);
  wifiScanStatusLabel_.font(&notosanslao_shaped_20);
  wifiScanStatusLabel_.textColor(0x52D3A5);
  wifiScanStatusLabel_.align(Align::Center, 0, 35);
  wifiScanStatusLabel_.text("ກຳລັງຄົ້ນຫາເຄືອຂ່າຍ Wi-Fi...");

  wifiScanList_ = lv_list_create(wifiScanPage_.native());
  lv_obj_set_size(wifiScanList_, 468, 260);
  lv_obj_set_pos(wifiScanList_, 6, 48);
  lv_obj_set_style_radius(wifiScanList_, 8, 0);
  lv_obj_set_style_bg_color(wifiScanList_, lv_color_hex(0x1B2A38), 0);
  lv_obj_set_style_border_color(wifiScanList_, lv_color_hex(0x2D3E4F), 0);
  lv_obj_set_style_border_width(wifiScanList_, 1, 0);
  lv_obj_set_style_pad_all(wifiScanList_, 4, 0);
  lv_obj_add_flag(wifiScanList_, LV_OBJ_FLAG_HIDDEN);
}

void WasherView::startWifiScan(uint8_t targetProfile) {
  scanTargetProfile_ = targetProfile;
  if (listener_) listener_->onWifiScanStarted();
  show(Page::WifiScan);

  String title = targetProfile == 0 ? "ຄົ້ນຫາສຳລັບ WiFi 1" : "ຄົ້ນຫາສຳລັບ WiFi 2";
  wifiScanTitle_.text(shapeLaoText(title.c_str()).c_str());

  if (wifiScanSpinner_) lv_obj_clear_flag(wifiScanSpinner_, LV_OBJ_FLAG_HIDDEN);
  wifiScanStatusLabel_.textColor(0x52D3A5);
  wifiScanStatusLabel_.text(shapeLaoText("ກຳລັງຄົ້ນຫາເຄືອຂ່າຍ Wi-Fi...").c_str());
  wifiScanStatusLabel_.hidden(false);
  if (wifiScanList_) lv_obj_add_flag(wifiScanList_, LV_OBJ_FLAG_HIDDEN);

  // Refresh display immediately so user sees the loading spinner & text
  lv_refr_now(NULL);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  WiFi.scanDelete();
  delay(100);

  int16_t status = WiFi.scanNetworks(false, false, false, 150);
  Serial.printf("[WiFiScan] scan completed: %d networks found\n", status);

  if (wifiScanSpinner_) lv_obj_add_flag(wifiScanSpinner_, LV_OBJ_FLAG_HIDDEN);

  if (wifiScanList_) {
    lv_obj_clean(wifiScanList_);
    lv_obj_clear_flag(wifiScanList_, LV_OBJ_FLAG_HIDDEN);

    if (status <= 0) {
      wifiScanStatusLabel_.text(shapeLaoText("ບໍ່ພົບເຄືອຂ່າຍ Wi-Fi ໃກ້ຄຽງ").c_str());
      wifiScanStatusLabel_.textColor(0xE85D55);
      wifiScanStatusLabel_.hidden(false);
      lv_obj_t* btn = lv_list_add_btn(wifiScanList_, NULL, shapeLaoText("ບໍ່ພົບເຄືອຂ່າຍ Wi-Fi ໃກ້ຄຽງ").c_str());
      lv_obj_set_style_text_font(btn, &notosanslao_shaped_20, 0);
      lv_obj_set_style_text_color(btn, lv_color_hex(0xE85D55), 0);
    } else {
      wifiScanStatusLabel_.hidden(true);
      scannedCount_ = status < 32 ? status : 32;
      for (uint16_t i = 0; i < scannedCount_; ++i) {
        scannedSsids_[i] = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        bool secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        if (scannedSsids_[i].isEmpty()) continue;

        const char* sigBar = (rssi >= -60) ? "[||||] 100%" :
                             (rssi >= -70) ? "[||| ] 75%"  :
                             (rssi >= -80) ? "[||  ] 50%"  : "[|   ] 25%";

        String itemText = "  " + scannedSsids_[i] + "   " + sigBar + (secured ? "  [LOCK]" : "");
        lv_obj_t* btn = lv_list_add_btn(wifiScanList_, NULL, itemText.c_str());
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x223446), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x2D3E4F), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_ver(btn, 8, 0);

        lv_obj_add_event_cb(btn, wifiListItemClicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
      }
    }
  }
  WiFi.scanDelete();
}

void WasherView::wifiScanEvent(lv_event_t* event) {
  if (!instance_) return;
  if (instance_->listener_) instance_->listener_->beep(1);
  const uint8_t profile = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  instance_->startWifiScan(profile);
}

void WasherView::wifiListItemClicked(lv_event_t* event) {
  if (!instance_) return;
  if (instance_->listener_) instance_->listener_->beep(1);
  uintptr_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  if (idx < instance_->scannedCount_) {
    const String& ssid = instance_->scannedSsids_[idx];
    if (instance_->scanTargetProfile_ == 0 && instance_->wifiSsidInput_) {
      lv_textarea_set_text(instance_->wifiSsidInput_, ssid.c_str());
    } else if (instance_->scanTargetProfile_ == 1 && instance_->wifiSsid2Input_) {
      lv_textarea_set_text(instance_->wifiSsid2Input_, ssid.c_str());
    }
  }
  if (instance_->listener_) instance_->listener_->onWifiScanEnded();
  instance_->show(Page::Settings);
}

void WasherView::wifiScanBackClicked(washerui::Object&, void*) {
  if (instance_ && instance_->listener_) instance_->listener_->beep(1);
  if (instance_) {
    instance_->scanningActive_ = false;
    WiFi.scanDelete();
    if (instance_->listener_) instance_->listener_->onWifiScanEnded();
    instance_->show(Page::Settings);
  }
}

void WasherView::wifiRescanClicked(washerui::Object&, void*) {
  if (instance_ && instance_->listener_) instance_->listener_->beep(1);
  if (instance_) instance_->startWifiScan(instance_->scanTargetProfile_);
}

void WasherView::wifiSwitchEvent(lv_event_t* event) {
  if (!instance_) return;
  if (instance_->listener_) instance_->listener_->beep(1);
  const uint8_t profile = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  bool state = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
  if (profile == 0) instance_->settings_->wifiEnabled1 = state;
  else instance_->settings_->wifiEnabled2 = state;
  instance_->settings_->save();
}

void WasherView::setActiveWifi(uint8_t profile, bool connected, const String& ssid, const String& ip) {
  static bool initialized = false;
  static uint8_t lastProfile = 0xFF;
  static bool lastConnected = false;
  static String lastSsid = "";
  static String lastIp = "";

  if (initialized && lastProfile == profile && lastConnected == connected && lastSsid == ssid && lastIp == ip) {
    return;
  }
  initialized = true;
  lastProfile = profile;
  lastConnected = connected;
  lastSsid = ssid;
  lastIp = ip;

  if (wifiBadge_[0]) {
    bool active1 = connected && profile == 0;
    lv_label_set_text(wifiBadge_[0], active1 ? "ONLINE" : "OFFLINE");
    lv_obj_set_style_text_color(wifiBadge_[0], lv_color_hex(active1 ? 0x20A765 : 0x8A9BA8), 0);
  }
  if (wifiBadge_[1]) {
    bool active2 = connected && profile == 1;
    lv_label_set_text(wifiBadge_[1], active2 ? "ONLINE" : "OFFLINE");
    lv_obj_set_style_text_color(wifiBadge_[1], lv_color_hex(active2 ? 0x20A765 : 0x8A9BA8), 0);
  }
  if (wifiStatusLabel_) {
    if (connected && !ssid.isEmpty()) {
      String text = "ເຊື່ອມຕໍ່: " + (profile == 0 ? String("WiFi 1") : String("WiFi 2")) + " (" + ssid + ")\nIP: " + ip;
      lv_label_set_text(wifiStatusLabel_, shapeLaoText(text.c_str()).c_str());
      lv_obj_set_style_text_color(wifiStatusLabel_, lv_color_hex(0x20A765), 0);
    } else {
      lv_label_set_text(wifiStatusLabel_, shapeLaoText("ສະຖານະ: ບໍ່ໄດ້ເຊື່ອມຕໍ່ (Offline)").c_str());
      lv_obj_set_style_text_color(wifiStatusLabel_, lv_color_hex(0xE85D55), 0);
    }
  }
}

void WasherView::wifiPassToggleEvent(lv_event_t* event) {
  if (!instance_) return;
  if (instance_->listener_) instance_->listener_->beep(1);
  const uint8_t idx = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  lv_obj_t* targetInput = (idx == 0) ? instance_->wifiPasswordInput_ : instance_->wifiPassword2Input_;
  if (!targetInput) return;

  bool isPass = lv_textarea_get_password_mode(targetInput);
  lv_textarea_set_password_mode(targetInput, !isPass);
  if (instance_->activeTargetTextArea_ == targetInput && instance_->keyboardPreviewInput_) {
    lv_textarea_set_password_mode(instance_->keyboardPreviewInput_, !isPass);
  }
}

void WasherView::brightnessEvent(lv_event_t*) {
  if (!instance_) return;
  const uint8_t value = lv_slider_get_value(instance_->brightnessSlider_);
  instance_->settings_->brightness = value;
  washerui::App.brightness(value);
  lv_label_set_text_fmt(instance_->brightnessValue_, "%u", value);
}

void WasherView::brightnessStepEvent(lv_event_t* event) {
  if (!instance_ || !instance_->settings_) return;
  const int diff = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  int val = static_cast<int>(instance_->settings_->brightness) + diff;
  if (val < 10) val = 10;
  if (val > 255) val = 255;
  instance_->settings_->brightness = static_cast<uint8_t>(val);
  if (instance_->brightnessSlider_) lv_slider_set_value(instance_->brightnessSlider_, val, LV_ANIM_OFF);
  if (instance_->brightnessValue_) lv_label_set_text_fmt(instance_->brightnessValue_, "%d", val);
  washerui::App.brightness(instance_->settings_->brightness);
  if (instance_->listener_) instance_->listener_->beep(1);
}

void WasherView::volumeEvent(lv_event_t* event) {
  if (!instance_ || !instance_->settings_ || !instance_->volumeSlider_) return;
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_RELEASED) {
    int val = lv_slider_get_value(instance_->volumeSlider_);
    if (val < 0) val = 0;
    if (val > 100) val = 100;
    instance_->settings_->volume = static_cast<uint8_t>(val);
    if (instance_->volumeValue_) lv_label_set_text_fmt(instance_->volumeValue_, "%d%%", val);
    washerui::App.volume(instance_->settings_->volume);
    if (code == LV_EVENT_RELEASED && instance_->listener_) {
      instance_->listener_->beep(1);
    }
  }
}

void WasherView::volumeStepEvent(lv_event_t* event) {
  if (!instance_ || !instance_->settings_) return;
  const int diff = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  int val = static_cast<int>(instance_->settings_->volume) + diff;
  if (val < 0) val = 0;
  if (val > 100) val = 100;
  instance_->settings_->volume = static_cast<uint8_t>(val);
  if (instance_->volumeSlider_) lv_slider_set_value(instance_->volumeSlider_, val, LV_ANIM_OFF);
  if (instance_->volumeValue_) lv_label_set_text_fmt(instance_->volumeValue_, "%d%%", val);
  washerui::App.volume(instance_->settings_->volume);
  if (instance_->listener_) instance_->listener_->beep(1);
}

void WasherView::wifiConnectEvent(lv_event_t* event) {
  if (!instance_ || !instance_->listener_) return;
  instance_->settings_->wifiSsid = lv_textarea_get_text(instance_->wifiSsidInput_);
  instance_->settings_->wifiPassword = lv_textarea_get_text(instance_->wifiPasswordInput_);
  instance_->settings_->wifiSsid2 = lv_textarea_get_text(instance_->wifiSsid2Input_);
  instance_->settings_->wifiPassword2 = lv_textarea_get_text(instance_->wifiPassword2Input_);
  if (instance_->wifiSwitch_[0]) instance_->settings_->wifiEnabled1 = lv_obj_has_state(instance_->wifiSwitch_[0], LV_STATE_CHECKED);
  if (instance_->wifiSwitch_[1]) instance_->settings_->wifiEnabled2 = lv_obj_has_state(instance_->wifiSwitch_[1], LV_STATE_CHECKED);
  instance_->settings_->save();
  instance_->setWifiStatus("WiFi: CONNECTING...");
  const uint8_t profile = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  const String& ssid = profile ? instance_->settings_->wifiSsid2 : instance_->settings_->wifiSsid;
  const String& password = profile ? instance_->settings_->wifiPassword2 : instance_->settings_->wifiPassword;
  instance_->listener_->onWifiConnectRequested(profile, ssid, password);
}

void WasherView::saveSettings() {
  if (activeTargetTextArea_ && keyboardPreviewInput_) {
    const char* txt = lv_textarea_get_text(keyboardPreviewInput_);
    lv_textarea_set_text(activeTargetTextArea_, txt ? txt : "");
  }
  settings_->terminalId = lv_textarea_get_text(terminalInput_);
  settings_->terminalId.trim();
  if (settings_->terminalId.isEmpty()) settings_->terminalId = "WASHER001";
  String pin = lv_textarea_get_text(pinInput_);
  pin.trim();
  if (pin.length() >= 4) settings_->settingsPin = pin;
  settings_->wifiSsid = lv_textarea_get_text(wifiSsidInput_);
  settings_->wifiPassword = lv_textarea_get_text(wifiPasswordInput_);
  settings_->wifiSsid2 = lv_textarea_get_text(wifiSsid2Input_);
  settings_->wifiPassword2 = lv_textarea_get_text(wifiPassword2Input_);
  if (wifiSwitch_[0]) settings_->wifiEnabled1 = lv_obj_has_state(wifiSwitch_[0], LV_STATE_CHECKED);
  if (wifiSwitch_[1]) settings_->wifiEnabled2 = lv_obj_has_state(wifiSwitch_[1], LV_STATE_CHECKED);
  settings_->brightness = lv_slider_get_value(brightnessSlider_);
  if (volumeSlider_) settings_->volume = lv_slider_get_value(volumeSlider_);
  for (uint8_t i = 0; i < 3; ++i) {
    if (timeDropdown_[i]) settings_->durationMinutes[i] = lv_dropdown_get_selected(timeDropdown_[i]) + 1;
  }
  settings_->save();
  washerui::App.brightness(settings_->brightness);
  washerui::App.volume(settings_->volume);
  if (keyboard_) lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
  if (keyboardPreviewCont_) lv_obj_add_flag(keyboardPreviewCont_, LV_OBJ_FLAG_HIDDEN);
  activeTargetTextArea_ = nullptr;

  if (saveBackdrop_) {
    lv_obj_clear_flag(saveBackdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(saveBackdrop_);
  }
  if (saveModal_) {
    if (saveModalBar_) lv_bar_set_value(saveModalBar_, 100, LV_ANIM_OFF);
    lv_obj_clear_flag(saveModal_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(saveModal_);
    triggerPopupBounce(saveModal_);
    saveModalHideAt_ = millis() + 1500;
  }
  Serial.printf("[SETTINGS] saved: TID=%s, pulses=[%u,%u,%u], durations=[%u,%u,%u]\n",
                settings_->terminalId.c_str(),
                settings_->pulseCount[0], settings_->pulseCount[1], settings_->pulseCount[2],
                settings_->durationMinutes[0], settings_->durationMinutes[1], settings_->durationMinutes[2]);

  if (listener_) listener_->onSettingsSaved();
}
