#include <Arduino.h>
#include <WasherUI.h>
#include <esp_task_wdt.h>

#include "AppSettings.h"
#include "WasherController.h"
#include "WasherView.h"

AppSettings settings;
WasherView view;
WasherController controller(settings, view);

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Washer Independent starting");

  // Initialize Task Watchdog Timer (30s timeout, restart on timeout)
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);

  settings.load();
  if (!washerui::App.begin(settings.brightness, settings.volume)) {
    Serial.println("Display initialization failed");
    return;
  }
  lv_font_glyph_dsc_t laoGlyph{};
  const bool laoFontReady = lv_font_get_glyph_dsc(&notosanslao_shaped_25, &laoGlyph, 0x0E81, 0);
  Serial.printf("Lao font U+0E81: %s\n", laoFontReady ? "ready" : "missing");
  Serial.printf("Lao stacked-mark shaping: %s\n", washerui::laoShapingSelfTest() ? "ready" : "failed");
  view.begin(settings, controller);
  controller.begin();
}

void loop() {
  esp_task_wdt_reset();
  static Page lastRenderedPage = Page::Settings;
  washerui::App.update();
  if (view.page() != lastRenderedPage) {
    lastRenderedPage = view.page();
    Serial.printf("[UI] rendered page %u, heap=%u\n",
                  static_cast<unsigned>(lastRenderedPage), ESP.getFreeHeap());
  }
  controller.update();
}

