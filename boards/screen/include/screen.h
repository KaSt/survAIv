#pragma once

#include <cstdint>

struct ScreenData {
  const char *status;       // "Running", "Idle", "Error"
  int countdown_secs;       // seconds until next cycle
  float equity;             // total equity in USD
  float cash;               // available cash in USD
  float pnl;                // absolute P&L in USD
  float pnl_pct;            // P&L percentage
  int cycle;                // cycle count
  int positions;            // current open positions
  int max_positions;        // max allowed
  const char *last_action;  // "buy_yes", "hold", etc.
  int accuracy_correct;     // correct decisions
  int accuracy_total;       // total resolved decisions
  bool paper_mode;          // true = paper, false = live
};

void screen_init();
void screen_update(const ScreenData &data);
void screen_set_backlight(bool on);
void screen_check_buttons();  // call periodically; handles timeout & wake
