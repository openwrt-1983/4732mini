// ui_lvgl.h —— 4732mini LVGL 界面层
//
// 界面层不读写 .ino 的全局变量，由调用方把要显示的内容打包成 UiState 传进来。
// 布局按 1.1.8G 录屏逐像素量取（320x170）。
#pragma once

#include <stdint.h>

struct UiState {
  // ---- 频率 ----
  uint16_t freqRaw;       // FM: 10kHz 单位(10390=103.90)；AM/SSB: 1kHz
  const char *bandName;   // "VHF" / "MW" / "SW8"
  const char *modeName;   // "FM" / "AM" / "LSB" / "USB"
  const char *timeText;   // "00:10:09"，空串则不显示

  // ---- 右列信号 ----
  uint8_t rssi;           // 0..127
  uint8_t snr;            // 0..127

  // ---- 音量 / 静音 ----
  uint8_t volume;         // 0..63
  bool muted;

  // ---- 底部 5 个胶囊 ----
  const char *stepText;   // "STEP-10"
  const char *bwText;     // "BW-AUT"
  const char *agcText;    // "AGC-ON"
  const char *avcText;    // "AVC-50"
  const char *smText;     // "SM-00"

  // ---- 电池 ----
  uint8_t battery;        // 0..100，255 = 未知
  bool charging;

  // ---- 状态行（频率下方中文）----
  const char *statusText; // "频率调节模式"

  // ---- 菜单（右侧白色面板）----
  bool menuMode;
  const char *const *menuItems;         // 菜单项名数组
  uint8_t menuCount;
  uint8_t menuIndex;
  const char *(*menuValueFn)(uint8_t);  // 取第 i 项的值文本，可为 nullptr
};

void uiInit();
void uiUpdate(const UiState &s);
void uiLoop();
void uiToast(const char *msg, uint32_t ms = 1200);
