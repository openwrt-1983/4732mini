// ui_lvgl.cpp —— 4732mini 界面层，按 1.1.8G 录屏逐像素复刻
//
// 坐标来源：tdisplay 录屏 854x480 归一化到 320x170 后做连通域分析得到。
// 屏幕 320x170，纯黑底。
// 配色不取自录屏（录屏全幅过曝到 255，颜色已失真），改用参考固件的配置导出
// MINI4732-CFG-V3 里的 51 项默认主题色。
#include "ui_lvgl.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <string.h>
#include <stdio.h>

#include "lvgl.h"
#include "font_big.h"     // Anton 57  —— 频率大字
#include "font_mid.h"     // Anton 24  —— MHz 单位
#include "font_num.h"     // Anton 15  —— 刻度数字
#include "font_cap.h"     // Anton 17  —— 胶囊内文字
#include "font_cn.h"      // 微米黑 18 加粗 —— 状态行
#include "font_cn16.h"    // 微米黑 15 —— 菜单项

// font_small 和 font_cap 是同一套字（Anton 17 / 同一字符集），之前各生成了一份
// 3.9KB 的位图，纯属重复。这里直接复用，省 3.9KB flash。
#define font_small font_cap

extern TFT_eSPI tft;

// LVGL 必须有时钟源：不挂 lv_tick，lv_timer_handler() 算出的 elapsed 恒为 0，
// 刷新定时器（LV_DEF_REFR_PERIOD 33ms）永远不触发 —— 表现就是刷完机一直黑屏。
// LVGL 9 用 lv_tick_set_cb 挂毫秒回调，不需要再手动 lv_tick_inc。
static uint32_t uiTickCb(void) { return (uint32_t)millis(); }

// ---------------- 配色（源自参考固件 MINI4732-config.txt 默认主题）----------------
#define C_BG     0x000000u   // radiobackgroundColor / frequencyColor / *backcolor
#define C_EDGE   0x01839Du   // radioiconcolor / *bodercolor / barbackcolor
#define C_TXT    0xCFCECEu   // frequencytextcolor / paramtextcolor / bartextcolor
#define C_TXT2   0xE9E9E9u   // bandtextcolor / radiotimetextcolor / radiovoltextcolor
#define C_SEL    0x004E61u   // selectedcolor
#define C_BATT   0x878787u   // radiobatbackcolor（电量格灰底）
#define C_RED    0xF00000u   // 刻度指针（主题表里没这一项，改自录屏实测红）
#define C_PRESET 0x89FA00u   // presetcolor（预设台标记，暂未用到）

// 菜单面板不在主题表的 51 项里，按录屏取中性白（实测 254/254/254，中性无偏色）
#define C_WHITE  0xFFFFFFu
#define C_GREY   0x9A9A9Au   // 菜单次要文字

#define SCR_W 320
#define SCR_H 170

// ---------------- 布局常量（量自录屏）----------------
// 顶部 6 个胶囊：信号图标 / 波段 / 模式 / 时间 / 音量 / 电量
static const int TOP_Y = 1, TOP_H = 20;
static const int TOP_X[6] = { 11, 36, 83, 130, 216, 266 };
static const int TOP_W[6] = { 21, 43, 42, 82, 48, 41 };

#define FREQ_CX     118   // 频率整体（数字+单位）的中心 x
#define FREQ_TOP    32    // 数字墨迹顶部 y
#define UNIT_TOP    60    // MHz 墨迹顶部 y
#define UNIT_GAP    8

#define STATUS_CX   119   // 中文状态行中心
#define STATUS_TOP  89    // 中文状态行墨迹顶部
#define SIDE_X      232   // SNR/RSSI 左边界
#define SIDE_W      80
#define SNR_TEXT_Y  35
#define SNR_BAR_Y   50
#define RSSI_TEXT_Y 63
#define RSSI_BAR_Y  78
#define BAR_H       5

// 底部 5 个胶囊
static const int BOT_Y = 114, BOT_H = 22;
static const int BOT_X[5] = { 4, 70, 135, 199, 261 };
static const int BOT_W[5] = { 64, 64, 62, 60, 58 };

#define TICK_Y      137   // 刻度线顶部
#define TICK_N      40
#define TICK_STEP   8
#define TICK_X0     6
#define TICK_SHORT  5
#define TICK_MID    9
#define TICK_LONG   15

#define TICKNUM_CX  88    // 三个刻度数字的中心：88 / 167 / 247
#define TICKNUM_DX  80
#define TICKNUM_Y   152
#define POINTER_X   166
#define POINTER_Y   138
#define POINTER_W   3
#define POINTER_H   16

// 菜单面板
#define MENU_X      175
#define MENU_Y      24
#define MENU_W      116
#define MENU_ROW_H  21
#define MENU_ROWS   5
#define MENU_H      (MENU_ROWS * MENU_ROW_H + 6)   // 5*21+上下内边距，防最后一行溢出

// ---------------- 对象 ----------------
static lv_display_t *disp;
static uint8_t *drawBuf;
static lv_obj_t *scr;

static lv_obj_t *cap[6];        // 顶部胶囊外框
static lv_obj_t *capTxt[6];     // 胶囊内文字
static lv_obj_t *capFill;       // 电量填充块
static lv_obj_t *capBattTip;    // 电池正极

static lv_obj_t *lblFreq, *lblUnit, *lblStatus;
static lv_obj_t *snrTxt, *snrVal, *snrBar;
static lv_obj_t *rssiTxt, *rssiVal, *rssiBar;

static lv_obj_t *botCap[5], *botTxt[5];
static lv_obj_t *tickBar[TICK_N];
static lv_obj_t *pointer;
static lv_obj_t *tickNum[3];

static lv_obj_t *menuPanel;
static lv_obj_t *menuRow[MENU_ROWS];
static lv_obj_t *menuName[MENU_ROWS];
static lv_obj_t *menuVal[MENU_ROWS];

// 文本缓存，避免每帧无谓重绘
static char cacheFreq[16], cacheUnit[8], cacheStatus[40];
static char cacheCap[6][16], cacheBot[5][16];
static char cacheMenu[MENU_ROWS][24], cacheMenuVal[MENU_ROWS][16];
static int  cacheMenuIdx = -1, cacheMenuCount = -1;

// ---------------- 刷新回调 ----------------
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
  uint32_t w = a->x2 - a->x1 + 1;
  uint32_t h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.pushColors((uint16_t *)px, w * h, true);
  tft.endWrite();
  lv_display_flush_ready(d);
}

// ---------------- 小工具 ----------------
static void setText(lv_obj_t *o, const char *s)
{
  if (!o || !s) return;
  const char *cur = lv_label_get_text(o);
  if (cur && strcmp(cur, s) == 0) return;
  lv_label_set_text(o, s);
}

static void setTextCache(lv_obj_t *o, char *cache, size_t n, const char *s)
{
  if (!s) s = "";
  if (strncmp(cache, s, n - 1) == 0) return;
  strncpy(cache, s, n - 1);
  cache[n - 1] = 0;
  lv_label_set_text(o, cache);
}

static lv_obj_t *makeCap(lv_obj_t *parent, int x, int y, int w, int h,
                         bool filled, const lv_font_t *font)
{
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, 5, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(C_EDGE), 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(filled ? C_EDGE : C_BG), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = lv_label_create(o);
  lv_obj_set_style_text_font(t, font, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(filled ? C_BG : C_TXT2), 0);
  lv_label_set_text(t, "");
  lv_obj_center(t);
  return o;
}

static lv_obj_t *labelAt(lv_obj_t *parent, int x, int y, const lv_font_t *font,
                         uint32_t color)
{
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, "");
  return l;
}

// ---------------- 构建 ----------------
static void buildTopBar()
{
  for (int i = 0; i < 6; i++) {
    // 信号图标和电量用实心，其它描边
    bool filled = (i == 0 || i == 5);
    cap[i] = makeCap(scr, TOP_X[i], TOP_Y, TOP_W[i], TOP_H, filled, &font_cap);
    capTxt[i] = lv_obj_get_child(cap[i], 0);
  }
  // 模式胶囊的文字比别的暗一档（modetextcolor #CFCECE vs bandtextcolor #E9E9E9）
  lv_obj_set_style_text_color(capTxt[2], lv_color_hex(C_TXT), 0);
  // 信号图标内部：斜线 + 左上小块 + 右下短杠（照录屏原样，整体像个 %）
  {
    static lv_point_precise_t pts[2] = { { 5, 16 }, { 16, 4 } };
    lv_obj_t *ln = lv_line_create(cap[0]);
    lv_line_set_points(ln, pts, 2);
    lv_obj_set_style_line_color(ln, lv_color_hex(C_BG), 0);
    lv_obj_set_style_line_width(ln, 2, 0);
  }
  {
    lv_obj_t *sq = lv_obj_create(cap[0]);
    lv_obj_set_style_bg_color(sq, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sq, 0, 0);
    lv_obj_set_style_radius(sq, 0, 0);
    lv_obj_set_style_pad_all(sq, 0, 0);
    lv_obj_remove_flag(sq, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(sq, 4, 4);
    lv_obj_set_pos(sq, 4, 3);
  }
  {
    lv_obj_t *bar = lv_obj_create(cap[0]);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, 4, 3);
    lv_obj_set_pos(bar, 13, 14);
  }

  // 音量胶囊：左侧喇叭图标（阶梯三角 + 音波），文字靠右
  {
    static const int bx[3] = { 7, 9, 11 }, by[3] = { 8, 6, 4 }, bh[3] = { 7, 11, 15 };
    for (int i = 0; i < 3; i++) {
      lv_obj_t *s = lv_obj_create(cap[4]);
      lv_obj_set_style_bg_color(s, lv_color_hex(C_TXT2), 0);
      lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(s, 0, 0);
      lv_obj_set_style_radius(s, 0, 0);
      lv_obj_set_style_pad_all(s, 0, 0);
      lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(s, 2, bh[i]);
      lv_obj_set_pos(s, bx[i], by[i]);
    }
    lv_obj_t *body = lv_obj_create(cap[4]);
    lv_obj_set_style_bg_color(body, lv_color_hex(C_TXT2), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(body, 3, 7);
    lv_obj_set_pos(body, 4, 8);
    lv_obj_t *wave = lv_obj_create(cap[4]);
    lv_obj_set_style_bg_color(wave, lv_color_hex(C_TXT2), 0);
    lv_obj_set_style_bg_opa(wave, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wave, 0, 0);
    lv_obj_set_style_radius(wave, 0, 0);
    lv_obj_set_style_pad_all(wave, 0, 0);
    lv_obj_remove_flag(wave, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(wave, 1, 7);
    lv_obj_set_pos(wave, 14, 7);
    lv_obj_align(capTxt[4], LV_ALIGN_RIGHT_MID, -6, 0);
  }
  // 电量：灰底 + 青色电量条 + 黑字（radiobatbackcolor / barcolor / textcolor）
  lv_obj_set_style_bg_color(cap[5], lv_color_hex(C_BATT), 0);
  // 正极画在胶囊右外侧（x = TOP_W-1，宽 3），默认会被父对象裁掉，只露 1px
  lv_obj_add_flag(cap[5], LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  capFill = lv_obj_create(cap[5]);
  lv_obj_set_style_bg_color(capFill, lv_color_hex(C_EDGE), 0);
  lv_obj_set_style_bg_opa(capFill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(capFill, 0, 0);
  lv_obj_set_style_radius(capFill, 4, 0);
  lv_obj_set_style_pad_all(capFill, 0, 0);
  lv_obj_remove_flag(capFill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(capFill, 0, TOP_H - 4);
  lv_obj_set_pos(capFill, 2, 2);
  lv_obj_add_flag(capFill, LV_OBJ_FLAG_HIDDEN);
  // 压到最底层，电量文字就永远在它上面，不必每帧 move_foreground 重排
  lv_obj_move_background(capFill);

  capBattTip = lv_obj_create(cap[5]);
  lv_obj_set_style_bg_color(capBattTip, lv_color_hex(C_EDGE), 0);
  lv_obj_set_style_bg_opa(capBattTip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(capBattTip, 0, 0);
  lv_obj_set_style_radius(capBattTip, 1, 0);
  lv_obj_set_style_pad_all(capBattTip, 0, 0);
  lv_obj_remove_flag(capBattTip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(capBattTip, 3, 8);
  lv_obj_set_pos(capBattTip, TOP_W[5] - 1, 6);

  // 电量文字要在填充块之上
  lv_obj_move_foreground(capTxt[5]);
}

static void buildFreq()
{
  lblFreq = labelAt(scr, 20, FREQ_TOP - 19, &font_big, C_TXT);
  lblUnit = labelAt(scr, 176, UNIT_TOP - 9, &font_mid, C_TXT);
  lblStatus = labelAt(scr, 0, STATUS_TOP - 1, &font_cn, C_TXT2);
  lv_obj_set_width(lblStatus, 238);
  lv_obj_set_style_text_align(lblStatus, LV_TEXT_ALIGN_CENTER, 0);
  // 状态文字万一超长，默认会换行成两行压到刻度区，改成超长省略
  // （LVGL 9.2 的枚举名是 LV_LABEL_LONG_DOT，9.3 才改成 LV_LABEL_LONG_MODE_DOTS）
  lv_label_set_long_mode(lblStatus, LV_LABEL_LONG_DOT);
}

static void buildSide()
{
  snrTxt = labelAt(scr, SIDE_X, SNR_TEXT_Y - 6, &font_small, C_TXT);
  lv_label_set_text(snrTxt, "SNR");
  snrVal = labelAt(scr, SIDE_X + SIDE_W - 10, SNR_TEXT_Y - 6, &font_small, C_TXT);
  lv_obj_set_style_text_align(snrVal, LV_TEXT_ALIGN_RIGHT, 0);
  snrBar = lv_obj_create(scr);
  lv_obj_set_pos(snrBar, SIDE_X, SNR_BAR_Y);
  lv_obj_set_size(snrBar, SIDE_W, BAR_H);
  lv_obj_set_style_bg_color(snrBar, lv_color_hex(C_EDGE), 0);
  lv_obj_set_style_bg_opa(snrBar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(snrBar, 0, 0);
  lv_obj_set_style_radius(snrBar, 2, 0);
  lv_obj_set_style_pad_all(snrBar, 0, 0);
  lv_obj_remove_flag(snrBar, LV_OBJ_FLAG_SCROLLABLE);

  rssiTxt = labelAt(scr, SIDE_X + 3, RSSI_TEXT_Y - 6, &font_small, C_TXT);
  lv_label_set_text(rssiTxt, "RSSI");
  rssiVal = labelAt(scr, SIDE_X + SIDE_W - 10, RSSI_TEXT_Y - 6, &font_small, C_TXT);
  rssiBar = lv_obj_create(scr);
  lv_obj_set_pos(rssiBar, SIDE_X + 3, RSSI_BAR_Y);
  lv_obj_set_size(rssiBar, SIDE_W, BAR_H);
  lv_obj_set_style_bg_color(rssiBar, lv_color_hex(C_EDGE), 0);
  lv_obj_set_style_bg_opa(rssiBar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rssiBar, 0, 0);
  lv_obj_set_style_radius(rssiBar, 2, 0);
  lv_obj_set_style_pad_all(rssiBar, 0, 0);
  lv_obj_remove_flag(rssiBar, LV_OBJ_FLAG_SCROLLABLE);
}

static void buildBottom()
{
  for (int i = 0; i < 5; i++) {
    botCap[i] = makeCap(scr, BOT_X[i], BOT_Y, BOT_W[i], BOT_H, false, &font_cap);
    botTxt[i] = lv_obj_get_child(botCap[i], 0);
  }
  // 刻度线：每 10 根一长，每 5 根一中（照录屏：长=整 MHz，中=半 MHz）
  for (int i = 0; i < TICK_N; i++) {
    int h = (i % 10 == 0) ? TICK_LONG : ((i % 5 == 0) ? TICK_MID : TICK_SHORT);
    lv_obj_t *t = lv_obj_create(scr);
    lv_obj_set_pos(t, TICK_X0 + i * TICK_STEP, TICK_Y);
    lv_obj_set_size(t, 2, h);
    lv_obj_set_style_bg_color(t, lv_color_hex(C_TXT), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_radius(t, 0, 0);
    lv_obj_set_style_pad_all(t, 0, 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    tickBar[i] = t;
  }
  // 红色指针
  pointer = lv_obj_create(scr);
  lv_obj_set_pos(pointer, POINTER_X, POINTER_Y);
  lv_obj_set_size(pointer, POINTER_W, POINTER_H);
  lv_obj_set_style_bg_color(pointer, lv_color_hex(C_RED), 0);
  lv_obj_set_style_bg_opa(pointer, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(pointer, 0, 0);
  lv_obj_set_style_radius(pointer, 1, 0);
  lv_obj_set_style_pad_all(pointer, 0, 0);
  lv_obj_remove_flag(pointer, LV_OBJ_FLAG_SCROLLABLE);

  // 三个刻度数字：以墨迹中心对齐到 TICKNUM_CX 起、每 TICKNUM_DX 一个
  for (int i = 0; i < 3; i++) {
    tickNum[i] = labelAt(scr, 0, TICKNUM_Y - 5, &font_num, C_TXT);
    lv_obj_set_style_text_align(tickNum[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(tickNum[i], TICKNUM_CX + i * TICKNUM_DX - 20, TICKNUM_Y - 5);
    lv_obj_set_width(tickNum[i], 40);
  }
}

static void buildMenu()
{
  menuPanel = lv_obj_create(scr);
  lv_obj_set_pos(menuPanel, MENU_X, MENU_Y);
  lv_obj_set_size(menuPanel, MENU_W, MENU_H);
  lv_obj_set_style_bg_color(menuPanel, lv_color_hex(C_WHITE), 0);
  lv_obj_set_style_bg_opa(menuPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(menuPanel, 0, 0);
  lv_obj_set_style_radius(menuPanel, 8, 0);
  lv_obj_set_style_pad_all(menuPanel, 3, 0);
  lv_obj_remove_flag(menuPanel, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < MENU_ROWS; i++) {
    lv_obj_t *r = lv_obj_create(menuPanel);
    lv_obj_set_pos(r, 0, i * MENU_ROW_H);
    lv_obj_set_size(r, MENU_W - 6, MENU_ROW_H);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 3, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    menuRow[i] = r;

    menuName[i] = lv_label_create(r);
    lv_obj_set_style_text_font(menuName[i], &font_cn16, 0);
    lv_obj_set_style_text_color(menuName[i], lv_color_hex(C_BG), 0);
    lv_label_set_text(menuName[i], "");
    lv_obj_align(menuName[i], LV_ALIGN_LEFT_MID, 6, 0);

    menuVal[i] = lv_label_create(r);
    lv_obj_set_style_text_font(menuVal[i], &font_small, 0);
    lv_obj_set_style_text_color(menuVal[i], lv_color_hex(C_GREY), 0);
    lv_obj_set_width(menuVal[i], MENU_W - 14);
    lv_obj_set_style_text_align(menuVal[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(menuVal[i], "");
    lv_obj_align(menuVal[i], LV_ALIGN_RIGHT_MID, -6, 0);
  }
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
}

// ---------------- 对外接口 ----------------
void uiInit()
{
  lv_init();
  lv_tick_set_cb(uiTickCb);   // 没有这一行，界面永远不会刷新

  disp = lv_display_create(SCR_W, SCR_H);
  // 缓冲放内部 RAM：320 x 40 x 2 = 25.6KB
  const uint32_t bufPx = SCR_W * 40;
  drawBuf = (uint8_t *)heap_caps_malloc(bufPx * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!drawBuf) drawBuf = (uint8_t *)heap_caps_malloc(bufPx * 2, MALLOC_CAP_8BIT);
  if (!drawBuf) {
    // 缓冲分配不出来就别往下走，否则 LVGL 会在空指针上画 —— 直接红屏报错
    tft.fillScreen(TFT_RED);
    while (1) delay(1000);
  }
  lv_display_set_buffers(disp, drawBuf, nullptr, bufPx * 2,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

  scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildTopBar();
  buildFreq();
  buildSide();
  buildBottom();
  buildMenu();

  cacheFreq[0] = cacheUnit[0] = cacheStatus[0] = 0;
  memset(cacheCap, 0, sizeof(cacheCap));
  memset(cacheBot, 0, sizeof(cacheBot));
}

static void updateMenu(const UiState &s)
{
  if (!s.menuMode || !s.menuItems || s.menuCount == 0) {
    if (!lv_obj_has_flag(menuPanel, LV_OBJ_FLAG_HIDDEN))
      lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
    cacheMenuIdx = -1;
    return;
  }
  if (lv_obj_has_flag(menuPanel, LV_OBJ_FLAG_HIDDEN))
    lv_obj_remove_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);

  // 选中项固定在第 3 行（索引 2），列表随选中项滚动
  int first = (int)s.menuIndex - 2;
  for (int i = 0; i < MENU_ROWS; i++) {
    int idx = first + i;
    if (idx < 0 || idx >= (int)s.menuCount) {
      setTextCache(menuName[i], cacheMenu[i], sizeof(cacheMenu[i]), "");
      setTextCache(menuVal[i], cacheMenuVal[i], sizeof(cacheMenuVal[i]), "");
      lv_obj_set_style_bg_opa(menuRow[i], LV_OPA_TRANSP, 0);
      continue;
    }
    setTextCache(menuName[i], cacheMenu[i], sizeof(cacheMenu[i]), s.menuItems[idx]);
    const char *v = s.menuValueFn ? s.menuValueFn((uint8_t)idx) : "";
    setTextCache(menuVal[i], cacheMenuVal[i], sizeof(cacheMenuVal[i]), v ? v : "");

    bool sel = (idx == (int)s.menuIndex);
    lv_obj_set_style_bg_color(menuRow[i], lv_color_hex(C_SEL), 0);
    lv_obj_set_style_bg_opa(menuRow[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(menuName[i],
                                lv_color_hex(sel ? C_TXT2 : C_BG), 0);
    lv_obj_set_style_text_color(menuVal[i],
                                lv_color_hex(sel ? C_TXT2 : C_GREY), 0);
  }
  cacheMenuIdx = s.menuIndex;
  cacheMenuCount = s.menuCount;
}

void uiUpdate(const UiState &s)
{
  char b[24];

  // ---- 频率 ----
  // freqRaw 是 SI4735 的 10kHz 单位：FM 10000 = 100.00MHz，AM 999 = 999kHz。
  // 原机 FM 只显示一位小数（录屏为 100.0 / 99.0），所以十分位取 (freqRaw%100)/10。
  bool fm = (s.modeName && s.modeName[0] == 'F');
  if (fm) {
    snprintf(b, sizeof(b), "%u.%u", (unsigned)(s.freqRaw / 100),
             (unsigned)((s.freqRaw % 100) / 10));
  } else {
    snprintf(b, sizeof(b), "%u", (unsigned)s.freqRaw);
  }
  setTextCache(lblFreq, cacheFreq, sizeof(cacheFreq), b);
  setTextCache(lblUnit, cacheUnit, sizeof(cacheUnit), fm ? "MHz" : "kHz");

  // 数字 + 单位作为一组，整体居中于 FREQ_CX
  lv_obj_update_layout(lblFreq);
  lv_obj_update_layout(lblUnit);
  int wf = lv_obj_get_width(lblFreq);
  int wu = lv_obj_get_width(lblUnit);
  int total = wf + UNIT_GAP + wu;
  int x0 = FREQ_CX - total / 2;
  if (x0 < 2) x0 = 2;
  lv_obj_set_pos(lblFreq, x0, FREQ_TOP - 19);
  lv_obj_set_pos(lblUnit, x0 + wf + UNIT_GAP, UNIT_TOP - 9);

  // ---- 顶部胶囊 ----
  setTextCache(capTxt[1], cacheCap[1], sizeof(cacheCap[1]),
               s.bandName ? s.bandName : "");
  setTextCache(capTxt[2], cacheCap[2], sizeof(cacheCap[2]),
               s.modeName ? s.modeName : "");
  setTextCache(capTxt[3], cacheCap[3], sizeof(cacheCap[3]),
               s.timeText ? s.timeText : "");

  if (s.muted) snprintf(b, sizeof(b), "MUTE");
  else snprintf(b, sizeof(b), "%02u", (unsigned)s.volume);
  setTextCache(capTxt[4], cacheCap[4], sizeof(cacheCap[4]), b);
  lv_obj_set_style_text_color(capTxt[4],
                              lv_color_hex(s.muted ? C_RED : C_TXT2), 0);

  if (s.battery == 255) {
    setTextCache(capTxt[5], cacheCap[5], sizeof(cacheCap[5]), "--");
  } else {
    snprintf(b, sizeof(b), "%u", (unsigned)s.battery);
    setTextCache(capTxt[5], cacheCap[5], sizeof(cacheCap[5]), b);
  }
  // 电量条按百分比铺满胶囊内宽
  {
    int bw = (s.battery == 255) ? 0 : (int)s.battery;
    if (bw > 100) bw = 100;
    bw = (TOP_W[5] - 4) * bw / 100;
    if (bw > 0) {
      if (lv_obj_get_width(capFill) != bw) lv_obj_set_width(capFill, bw);
      lv_obj_remove_flag(capFill, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(capFill, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // ---- 状态行 ----
  setTextCache(lblStatus, cacheStatus, sizeof(cacheStatus),
               s.statusText ? s.statusText : "");

  // ---- SNR / RSSI ----
  snprintf(b, sizeof(b), "%u", (unsigned)s.snr);
  setText(snrVal, b);
  snprintf(b, sizeof(b), "%u", (unsigned)s.rssi);
  setText(rssiVal, b);

  // ---- 底部胶囊 ----
  const char *bt[5] = { s.bwText, s.stepText, s.agcText, s.avcText, s.smText };
  for (int i = 0; i < 5; i++)
    setTextCache(botTxt[i], cacheBot[i], sizeof(cacheBot[i]), bt[i] ? bt[i] : "");

  // ---- 刻度数字：左 / 中 / 右 ----
  if (fm) {
    int f = s.freqRaw;   // 10kHz 单位
    snprintf(b, sizeof(b), "%d.%d", (f - 100) / 100, ((f - 100) % 100) / 10);
    setText(tickNum[0], b);
    snprintf(b, sizeof(b), "%d.%d", f / 100, (f % 100) / 10);
    setText(tickNum[1], b);
    snprintf(b, sizeof(b), "%d.%d", (f + 100) / 100, ((f + 100) % 100) / 10);
    setText(tickNum[2], b);
  } else {
    snprintf(b, sizeof(b), "%d", (int)s.freqRaw - 10);
    setText(tickNum[0], b);
    snprintf(b, sizeof(b), "%d", (int)s.freqRaw);
    setText(tickNum[1], b);
    snprintf(b, sizeof(b), "%d", (int)s.freqRaw + 10);
    setText(tickNum[2], b);
  }

  updateMenu(s);
}

void uiLoop()
{
  lv_timer_handler();
}

void uiToast(const char *msg, uint32_t ms)
{
  if (!msg) return;
  lv_obj_t *m = lv_obj_create(scr);
  lv_obj_set_style_bg_color(m, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(m, LV_OPA_90, 0);
  lv_obj_set_style_border_width(m, 2, 0);
  lv_obj_set_style_border_color(m, lv_color_hex(C_EDGE), 0);
  lv_obj_set_style_radius(m, 6, 0);
  lv_obj_set_style_pad_all(m, 8, 0);
  lv_obj_remove_flag(m, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *l = lv_label_create(m);
  lv_obj_set_style_text_font(l, &font_cn, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(C_TXT2), 0);
  lv_label_set_text(l, msg);
  lv_obj_center(m);
  lv_obj_center(l);
  lv_obj_delete_delayed(m, ms);
}
