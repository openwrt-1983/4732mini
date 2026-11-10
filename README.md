# 4732mini（LilyGO T-Display-S3 + SI4732/SI4735 收音机固件）

## ⚠️ 来源声明

**本仓库不是原创项目，原地址：<https://github.com/zhang-chong/4732mini>**

- 原作者：**zhang-chong**（老张）
- 本仓库基于上游提交 `5334e7b`（2026-03-07，`Create CNAME`）派生
- 原作者声明：**仅供学习交流使用，请勿用作商业用途**。如有改进希望回馈上游，一起优化。
- 本仓库做了两件事：① 在原作者代码基础上修 bug（根目录这版，界面交互全部沿用原设计）；② 另外派生了一套 LVGL 界面版（`4732mini-lvgl/`，业务层同源、界面层重画，见文末）。

如果你要找的是原始项目、最新版或想给作者提改进，请去上面的原地址。

---

## 硬件

| 项 | 内容 |
|---|---|
| 主控 | ESP32-S3（LilyGO **T-Display-S3** 一体板） |
| 屏幕 | 板载 ST7789，**8 位并行**接口（CS 6 / DC 7 / RST 5 / WR 8 / RD 9 / D0–D7 = 39,40,41,42,45,46,47,48），背光 GPIO38 走 PWM |
| 收音 | SI4732 / SI4735（I²C），`patch_init.h` 是 SSB 补丁 |
| 编码器 | EC11（A 相 GPIO2、B 相 GPIO1、按压 GPIO21） |
| 电池 | 18650，ADC 采样用于电量显示 |

## 编译

```bash
# 仓库根目录的 .ino 名字和目录名不一致，先复制成同名 sketch
mkdir -p ~/build-4732/4732mini
cp *.cpp *.h ~/build-4732/4732mini/
cp ALL_IN_ONE_T-Display_S3.ino ~/build-4732/4732mini/4732mini.ino

cd ~/build-4732/4732mini
arduino-cli compile -b esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=huge_app \
  --libraries <本仓库>/libraries --build-path ~/build-4732/out
```

两个注意点：

1. **必须用仓库自带的 `libraries/TFT_eSPI_DynamicSpeed`**，不要装官方 TFT_eSPI。前者的 `User_Setup_Select.h` 启用了 `User_Setups/Setup206_LilyGo_T_Display_S3.h`，那是 8 位并行配置；装官方库会白屏。
2. 原代码用了 `ledcSetup` / `ledcAttachPin`（arduino-esp32 **2.x** API，3.x 已移除）。本仓库已加版本分支，**2.x / 3.x 都能编**。

## 固件

分区按 `16M + huge_app`（app 3 MB @ `0x10000`）：

| 文件 | 地址 | 说明 |
|---|---|---|
| `4732mini.ino.bin` | `0x10000` | 只刷应用，保留设置与电台记忆 |
| `4732mini.ino.merged.bin` | `0x0` | 整片 16 MB，**会清空设置和电台** |
| `4732mini.ino.bootloader.bin` | `0x0` | |
| `4732mini.ino.partitions.bin` | `0x8000` | |
| `boot_app0.bin` | `0xe000` | |

刷机建议用 <https://espressif.github.io/esptool-js/>（Chrome）。

## 本仓库修复的问题

上游代码在功能主干上是通的，以下问题多为「一路加功能、没回头收口」积累下来的。本次逐条修掉，行为与界面尽量保持原样。

### 会崩 / 会费电 / 会丢数据

1. **熄屏浅睡唤醒源错误**：原来 `esp_sleep_enable_ext0_wakeup(GPIO_NUM_1, LOW)` 是**电平**触发，EC11 静止时 GPIO1 常为低电平，进睡即刻满足唤醒条件 → 熄屏后 80 MHz 空转、掉电反而更快；且只挂了 B 相，**按键按不醒**。已改为浅睡专用的 `gpio_wakeup_enable()` + `esp_sleep_enable_gpio_wakeup()`：A 相 / B 相 / 按键三个脚都挂上，唤醒电平按「当前电平的反相」设置（静止时稳定、一动就醒），每次浅睡退出后重新挂回编码器的 CHANGE 中断，唤醒后直接亮屏。
2. **EEPROM 回读无边界校验**：`bandIdx` / `currentMode` / 步进 / 带宽 / 频率等直接从 EEPROM 读出就用，一旦数据损坏（读回 255）就是 `band[255]` 之类的越界写 → 随机死机、白屏、重启循环。现已全部钳位到合法区间，非法值回落默认。
3. **`station[50]` 越界**：数组 50 项，`readsavednumcount()` 从 1 写到 50，`station[50]` 越界；菜单里 `station[selnum]` 同样越界读。数组改 51，`storenumber` 显式 50，读写都校验 1–50。

### 功能错误

4. `drawMenu()` 的 `if (bandType == MW_BAND_TYPE || SW_BAND_TYPE)` —— 常量恒真，FM 下会把 AM 与 FM 两套带宽文字画在同一位置。补全比较。
5. `dobright()`：`brightness` 是 `unsigned char`，`1 - 5` 回绕成 252，最暗直接跳最亮。改 `int` 运算后钳位。
6. 电量档位在 **6%–14% 之间没有分支**，百分比停在上一次的值不动。补 `else`。
7. `chargechanged` 恒为 `false`，充电中每帧做 20 次 ADC 采样。改为真正的状态比较。
8. **连点 4 下会被当成电池校准**，直接改写 `CONV_FACTOR` 并落 EEPROM；ADC 读到 0 时除零 → 系数变 `inf`。加双重闸门（电压 3.0–4.6 V、系数 1.0–3.0）。
9. `useBand()` 的 MW 分支不重置 `ssbLoaded`：SW→USB→MW→SW 之后会出现「界面显示 AM、实际输出 LSB」。SSB 分支补 `currentMode == LSB || USB` 条件。
10. `getStrength()` **缺 return**：AM 下 `rssi == 2`、FM 下 `rssi == 1` 会走完所有判断落空，返回寄存器残留值，信号格数随机跳。重写为逐级 `<=`，每个取值都有返回值。
11. `getBatteryVolts(enable == false)` 分支缺 `return`（潜伏问题，当前调用路径恰好绕开）。补 `return 0`，ADC 读到 0 也返回 0。

### 性能 / 闪存寿命

12. `DrawBatteryLevel()` 内部 `pushSprite()`，`drawSprite()` 末尾又推一次 → 每帧两次 106 KB SPI。去掉内部那次。
13. `doselstation()` 每格调 3 次 `readsavednumcount()`（每次 50 次 EEPROM 读），电台列表转动发涩。改为读 1 遍。
14. `saveAllReceiverInformation()` 每次保存 21 次 `EEPROM.commit()`。合并为 1 次；顺带把 AGC / SoftMute 也纳入持久化（EEPROM 12/13 字节，不与既有数据冲突）。

### 隐患

15. `showRDSMsg()` / `showRDSTime()` 往 SI4735 库内部缓冲 `rdsMsg[35]` 写 `'\0'`，改为不写库内存。
16. `doabout()` 里 `while (digitalRead(21));` 按键卡住即整机挂死，加 3 秒超时。
17. `%4.4d` 打负数得 `-0120`，改 `%+d`（BFO 显示）。
18. `lcdofftime` 被钳在 1..99，自动熄屏永远关不掉。允许 0 = 关闭。
19. `sprintf` 全部换 `snprintf`；`ICACHE_RAM_ATTR` → `ARDUINO_ISR_ATTR`（3.x）。

## 尚未验证

以下只有真机能确认，刷上去第一件事建议看这三样：

- 熄屏后的电池电流（应降到个位数 mA）
- 熄屏状态下**按一下按键**能否亮屏，以及旋钮唤醒是否即时
- 电量百分比（esp32 core 3.x 的 ADC 实现与 2.x 略有差异，若偏几个点，摇一次四连击重新校准即可）

---

# LVGL 界面版（`4732mini-lvgl/`）

同一块板子、同一套业务代码，界面改用 **LVGL 9.2.2** 重画。

## 和根目录这版的关系

- **业务层同源**：`4732mini-lvgl.ino` 从 `ALL_IN_ONE_T-Display_S3.ino` 派生。调谐、波段、SSB 补丁、EEPROM 记忆、编码器、电池这些逻辑原样保留，上面 19 条修复也全部在内。
- **界面层重写**：原版 200 多处直接 `spr.drawSprite()` 的像素绘制全部删除，改由 `ui_lvgl.cpp` 用 LVGL 对象绘制。`drawSprite()` 只负责把当前频率 / 信号 / 音量 / 电量 / 菜单打包成 `UiState` 交给 `uiUpdate()`，界面层不碰业务全局变量。
- 两版并存、互不影响：根目录仍是「原版界面 + 修 bug」，`4732mini-lvgl/` 是新界面。

## 目录

```
4732mini-lvgl/
  4732mini-lvgl.ino     业务层（派生自根目录 .ino，去掉绘图）
  ui_lvgl.h / .cpp      界面层（LVGL 对象、布局、配色）
  font_big.h            Anton 57  频率大字
  font_mid.h            Anton 24  MHz / 中号
  font_cap.h            Anton 17  胶囊文字
  font_num.h            Anton 15  刻度数字
  font_cn.h             微米黑 18 加粗  状态行 / 菜单
  font_cn16.h           微米黑 16  次级中文
  Battery.* Rotary.* patch_init.h icon.h   与根目录同源
  lv_conf.h.example     lv_conf 模板
  build.sh              编译脚本
```

## 依赖

| 项 | 说明 |
|---|---|
| LVGL | **9.2.2**，装在 `$HOME/Arduino/libraries/lvgl` |
| lv_conf.h | 复制 `4732mini-lvgl/lv_conf.h.example` 到 `$HOME/Arduino/libraries/lv_conf.h`，关键项：`LV_COLOR_DEPTH 16`、`LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB`、`LV_FONT_SIMSUN_16_CJK 1` |
| TFT_eSPI | 仍用仓库自带的 `libraries/TFT_eSPI_DynamicSpeed`（8 位并行 Setup206），**不要装官方库** |

## 编译

```bash
./4732mini-lvgl/build.sh
```

手工等价命令：

```bash
arduino-cli compile -b esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=huge_app \
  --libraries <本仓库>/libraries \
  --build-path ~/build-4732/out-lvgl \
  --output-dir <本仓库>/out-lvgl \
  4732mini-lvgl
```

分区与根目录版一致：16 MB Flash + `huge_app`（app 3 MB @ `0x10000`）。
当前体积：**830,785 B（26%）**，静态 RAM **28,728 B（8%）**。

## 界面内容

- 顶部 6 个胶囊：信号图标 / 波段 / 模式 / 时间 / 音量 / 电量
- 频率大字（Anton 57px）+ MHz 单位，下方中文状态行（微米黑 18 加粗）
- 右列 SNR / RSSI：竖排数字 + 青色条
- 底部 5 个参数胶囊（步进 / 带宽 / 增益 / 频率搜索）、40 格刻度尺、红色指针
- 菜单为中央覆盖层，5 行，选中项固定在第 3 行

配色取自参考固件导出的主题配置：底 `#000000`、边 `#01839D`、正文 `#CFCECE`、次级文字 `#E9E9E9`、选中 `#004E61`、电池底 `#878787`、指针 `#F00000`。

## LVGL 版修掉的坑

1. **`lv_init()` 之后没设 tick**：`lv_timer_handler()` 靠 `lv_tick_get()` 算 elapsed，tick 恒 0 → 刷新定时器永不触发，屏幕停在 `fillScreen(TFT_BLACK)`。加 `lv_tick_set_cb(millis)`。
2. **背光开得太晚**：SI4735 检测失败时 `while(1)` 卡住，而背光还是 0，整机看起来毫无反应。把 PWM 初始化提到 `uiInit()` 之前。
3. **字体斑马线**：LVGL 4bpp 位图是**连续 nibble 流、行间不补齐**，按「每行 ceil(w/2) 字节」打包会让奇数宽度字形错位。已按连续流重打包，`decode_font.py` 可离线解码校验。
4. **中文多 1px 空白且偏左**：`getbbox()` 的返回框已含描边，mkfont 里又加了一次 pad。
5. **电池正极被裁**：`capBattTip` 超出父对象宽度，加 `LV_OBJ_FLAG_OVERFLOW_VISIBLE`。
6. 频率多打一位小数、菜单行内中文与数值垂直错位。
7. `lv_label` 长文本模式在 9.2 里叫 `LV_LABEL_LONG_DOT`（9.3 才改名 `LV_LABEL_LONG_MODE_DOTS`）。

## 尚未验证

- 真机配色与字号微调（参考固件是拍屏/录屏反推的，色值来自配置导出，亮度可能有偏差）
- 菜单仍是覆盖层，不是真正的 LVGL 列表（阶段二）
- 熄屏唤醒后的补帧

