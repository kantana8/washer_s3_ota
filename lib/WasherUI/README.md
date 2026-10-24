# WasherUI

An independent, small C++ UI layer for the ESP32-3248S035C washer controller.
It uses LVGL and LovyanGFX directly and does not link BlynkGO libraries.

The first prototype includes:

- ST7796 display and GT911 capacitive-touch configuration for PCB V1.4
- LVGL display, input, tick, and render loop integration
- Lazy-created `Object`, `Rect`, `Label`, and `Button` wrappers
- Basic style, positioning, visibility, and click-event APIs

See `experiments/washer_ui_smoke` for the hardware smoke test.
