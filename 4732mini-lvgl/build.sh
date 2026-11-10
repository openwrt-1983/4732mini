#!/usr/bin/env bash
#
# 编译 4732mini-lvgl（LVGL 9 界面版）
#
# 依赖：
#   - LVGL 9.2.2 装在 $HOME/Arduino/libraries/lvgl
#   - 配置文件 $HOME/Arduino/libraries/lv_conf.h（可从本目录 lv_conf.h.example 复制后改名）
#   - TFT_eSPI 用仓库自带的 libraries/TFT_eSPI_DynamicSpeed（8 位并行 Setup206）
#
# 产物：<仓库>/out-lvgl/*.bin
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH="$ROOT/4732mini-lvgl"
OUT="${OUT:-$HOME/build-4732/out-lvgl}"
DEST="$ROOT/out-lvgl"
CLI="${ARDUINO_CLI:-$HOME/bin/arduino-cli}"

mkdir -p "$OUT" "$DEST"

TMPDIR="${TMPDIR:-$HOME/build-tmp}" "$CLI" compile \
  -b esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=huge_app \
  --libraries "$ROOT/libraries" \
  --build-path "$OUT" \
  --output-dir "$DEST" \
  --warnings default \
  "$SKETCH"

echo
ls -l "$DEST"/*.bin | awk '{print $5, $9}'
md5sum "$DEST"/*.bin
