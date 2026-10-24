#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace washerui {

String shapeLaoText(const char* text);
bool laoShapingSelfTest();

}  // namespace washerui

extern const lv_font_t notosanslao_shaped_25;
extern const lv_font_t notosanslao_shaped_20;
