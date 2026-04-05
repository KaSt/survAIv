// On-device LCD display for boards with built-in screens.
// Uses LovyanGFX for SPI LCD driving and text rendering.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "screen.h"

#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *kTag = "screen";

// ── Pin definitions per board ────────────────────────────────────────────────

#if defined(CONFIG_SURVAIV_DISPLAY_GC9107_TQT)
  // LilyGO T-QT Pro  (ESP32-S3, GC9107 128×128)
  #define PIN_SCLK    3
  #define PIN_MOSI    2
  #define PIN_DC      6
  #define PIN_CS      5
  #define PIN_RST     1
  #define PIN_BL     10
  #define SCR_W     128
  #define SCR_H     128
  #define SCR_ROT     0
  #define SCR_OFF_X   1
  #define SCR_OFF_Y   0
  #define BTN_A      GPIO_NUM_0
  #define BTN_B      GPIO_NUM_47
  using PanelType = lgfx::Panel_GC9107;

#elif defined(CONFIG_SURVAIV_DISPLAY_GC9107_ATOMS3)
  // M5Stack AtomS3  (ESP32-S3, GC9107 128×128)
  #define PIN_SCLK   17
  #define PIN_MOSI   21
  #define PIN_DC     33
  #define PIN_CS     15
  #define PIN_RST    34
  #define PIN_BL     16
  #define SCR_W     128
  #define SCR_H     128
  #define SCR_ROT     2
  #define SCR_OFF_X   2
  #define SCR_OFF_Y   1
  #define BTN_A      GPIO_NUM_41
  using PanelType = lgfx::Panel_GC9107;

#elif defined(CONFIG_SURVAIV_DISPLAY_ST7789_STICKC2)
  // M5StickC PLUS2  (ESP32, ST7789V2 135×240)
  #define PIN_SCLK   13
  #define PIN_MOSI   15
  #define PIN_DC     14
  #define PIN_CS      5
  #define PIN_RST    12
  #define PIN_BL     27
  #define SCR_W     135
  #define SCR_H     240
  #define SCR_ROT     1   // landscape: 240×135
  #define SCR_OFF_X  52
  #define SCR_OFF_Y  40
  #define BTN_A      GPIO_NUM_37
  #define BTN_B      GPIO_NUM_39
  using PanelType = lgfx::Panel_ST7789;

#else
  #error "No display board type selected in Kconfig"
#endif

// ── LovyanGFX device ────────────────────────────────────────────────────────

class LGFX : public lgfx::LGFX_Device {
  PanelType      _panel;
  lgfx::Bus_SPI  _bus;
  lgfx::Light_PWM _light;

 public:
  LGFX() {
    {
      auto cfg       = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.freq_write = 27000000;
      cfg.pin_sclk   = PIN_SCLK;
      cfg.pin_mosi   = PIN_MOSI;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = PIN_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg            = _panel.config();
      cfg.pin_cs          = PIN_CS;
      cfg.pin_rst         = PIN_RST;
      cfg.memory_width    = SCR_W;
      cfg.memory_height   = SCR_H;
      cfg.panel_width     = SCR_W;
      cfg.panel_height    = SCR_H;
      cfg.offset_x        = SCR_OFF_X;
      cfg.offset_y        = SCR_OFF_Y;
      _panel.config(cfg);
    }
    {
      auto cfg        = _light.config();
      cfg.pin_bl      = PIN_BL;
      cfg.invert      = false;
      cfg.freq        = 12000;
      cfg.pwm_channel = 0;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

// ── State ────────────────────────────────────────────────────────────────────

static LGFX lcd;
static bool bl_on          = true;
static int64_t last_touch  = 0;
static int timeout_sec     = 30;

// Colours
static constexpr uint32_t BG     = 0x000000;
static constexpr uint32_t FG     = 0xFFFFFF;
static constexpr uint32_t CYAN   = 0x00FFFF;
static constexpr uint32_t GREY   = 0x888888;
static constexpr uint32_t GREEN  = 0x00FF00;
static constexpr uint32_t RED    = 0xFF0000;
static constexpr uint32_t YELLOW = 0xFFFF00;
static constexpr uint32_t ORANGE = 0xFF8800;

// ── Helpers ──────────────────────────────────────────────────────────────────

static void setup_button(gpio_num_t pin) {
  gpio_config_t io = {};
  io.mode          = GPIO_MODE_INPUT;
  io.pull_up_en    = GPIO_PULLUP_ENABLE;
  io.pin_bit_mask  = 1ULL << pin;
  gpio_config(&io);
}

static bool any_button_pressed() {
  if (gpio_get_level(BTN_A) == 0) return true;
#ifdef BTN_B
  if (gpio_get_level(BTN_B) == 0) return true;
#endif
  return false;
}

// Draw a label+value pair. Label in grey, value in given colour.
static void draw_kv(int x, int y, const char *label, const char *value,
                    uint32_t val_colour = FG) {
  lcd.setTextColor(GREY, BG);
  lcd.setCursor(x, y);
  lcd.print(label);
  lcd.setTextColor(val_colour, BG);
  lcd.print(value);
}

// ── Public API ───────────────────────────────────────────────────────────────

void screen_init() {
  lcd.init();
  lcd.setRotation(SCR_ROT);
  lcd.fillScreen(BG);
  lcd.setTextWrap(false);

  setup_button(BTN_A);
#ifdef BTN_B
  setup_button(BTN_B);
#endif

  last_touch  = esp_timer_get_time();
  timeout_sec = CONFIG_SURVAIV_SCREEN_TIMEOUT_SEC;

  // Boot splash
  lcd.setTextColor(CYAN, BG);
  lcd.setTextSize(2);
  int cx = (lcd.width() - 7 * 12) / 2;
  int cy = lcd.height() / 2 - 16;
  if (cx < 0) cx = 0;
  lcd.setCursor(cx, cy);
  lcd.print("SURVAIV");
  lcd.setTextSize(1);
  lcd.setTextColor(GREY, BG);
  lcd.setCursor(cx, cy + 24);
  lcd.print("Starting...");
  lcd.setBrightness(128);

  ESP_LOGI(kTag, "Display %dx%d initialised", lcd.width(), lcd.height());
}

void screen_update(const ScreenData &d) {
  if (!bl_on) return;

  lcd.startWrite();
  lcd.fillScreen(BG);

  // Choose layout based on display size.
  // Landscape 240×135 (StickC) vs. 128×128 (TQT/Atoms3).
  const bool wide = lcd.width() >= 200;
  const int font_sz = wide ? 1 : 1;  // both use 6×8 base font at size 1
  const int lh  = wide ? 12 : 10;    // line height
  const int pad = 2;

  lcd.setTextSize(font_sz);
  int y = pad;

  // ── Header ──
  lcd.setTextColor(CYAN, BG);
  lcd.setCursor(pad, y);
  lcd.print("SURVAIV");

  // Mode badge
  lcd.setTextColor(d.paper_mode ? YELLOW : GREEN, BG);
  lcd.setCursor(lcd.width() - (d.paper_mode ? 30 : 24) - pad, y);
  lcd.print(d.paper_mode ? "PAPER" : "LIVE");
  y += lh;

  // Status + countdown
  lcd.setCursor(pad, y);
  uint32_t sc = GREEN;
  if (d.status && strcmp(d.status, "Error") == 0) sc = RED;
  lcd.setTextColor(sc, BG);
  lcd.print(d.status ? d.status : "---");

  if (d.countdown_secs > 0) {
    char buf[16];
    snprintf(buf, sizeof(buf), " %dm%02ds", d.countdown_secs / 60,
             d.countdown_secs % 60);
    lcd.setTextColor(GREY, BG);
    lcd.print(buf);
  }
  y += lh;

  // Separator
  lcd.drawFastHLine(pad, y, lcd.width() - 2 * pad, GREY);
  y += 4;

  // ── Portfolio ──
  char buf[32];

  snprintf(buf, sizeof(buf), "$%.2f", (double)d.equity);
  draw_kv(pad, y, "Equity ", buf);
  y += lh;

  snprintf(buf, sizeof(buf), "$%.2f", (double)d.cash);
  draw_kv(pad, y, "Cash   ", buf);
  y += lh;

  snprintf(buf, sizeof(buf), "%+.2f", (double)d.pnl);
  draw_kv(pad, y, "P&L    ", buf, d.pnl >= 0 ? GREEN : RED);
  y += lh;

  snprintf(buf, sizeof(buf), "%+.1f%%", (double)d.pnl_pct);
  draw_kv(pad, y, "       ", buf, d.pnl_pct >= 0 ? GREEN : RED);
  y += lh + 2;

  // Separator
  lcd.drawFastHLine(pad, y, lcd.width() - 2 * pad, GREY);
  y += 4;

  // ── Activity ──
  snprintf(buf, sizeof(buf), "%d", d.cycle);
  draw_kv(pad, y, "Cycle  ", buf);
  y += lh;

  snprintf(buf, sizeof(buf), "%d/%d", d.positions, d.max_positions);
  draw_kv(pad, y, "Pos    ", buf);
  y += lh;

  if (d.last_action && d.last_action[0]) {
    draw_kv(pad, y, "Last   ", d.last_action, YELLOW);
    y += lh;
  }

  // ── Accuracy (if resolved data exists) ──
  if (d.accuracy_total > 0) {
    snprintf(buf, sizeof(buf), "%d/%d", d.accuracy_correct, d.accuracy_total);
    uint32_t acc_col =
        (d.accuracy_correct * 2 >= d.accuracy_total) ? GREEN : ORANGE;
    draw_kv(pad, y, "Acc    ", buf, acc_col);
    y += lh;
  }

  // On wide display, show extra info in right column
  if (wide && y < lcd.height() - lh) {
    // Nothing extra for now — room for future stats
  }

  lcd.endWrite();
}

void screen_set_backlight(bool on) {
  bl_on = on;
  lcd.setBrightness(on ? 128 : 0);
}

void screen_check_buttons() {
  int64_t now = esp_timer_get_time();

  if (any_button_pressed()) {
    last_touch = now;
    if (!bl_on) {
      screen_set_backlight(true);
    }
  }

  // Auto-off after timeout
  if (bl_on && timeout_sec > 0) {
    int64_t elapsed = (now - last_touch) / 1000000;
    if (elapsed >= timeout_sec) {
      screen_set_backlight(false);
    }
  }
}
