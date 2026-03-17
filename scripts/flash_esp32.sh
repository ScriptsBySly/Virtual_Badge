#!/usr/bin/env bash
set -euo pipefail

PORT=${PORT:-/dev/ttyUSB0}
BAUD=${BAUD:-460800}
OUT_DIR=${OUT_DIR:-output}
BUILD_DIR=${BUILD_DIR:-build}
ESPTOOL=${ESPTOOL:-}

usage() {
  echo "Usage: $0 [-d /dev/ttyUSB0] [-b 460800] [-o output] [-B build]"
}

while getopts ":d:b:o:B:h" opt; do
  case "${opt}" in
    d) PORT="${OPTARG}" ;;
    b) BAUD="${OPTARG}" ;;
    o) OUT_DIR="${OPTARG}" ;;
    B) BUILD_DIR="${OPTARG}" ;;
    h) usage; exit 0 ;;
    *) usage; exit 1 ;;
  esac
done

APP_BIN="${OUT_DIR}/virtual_badge_esp32.bin"
BOOT_BIN="${OUT_DIR}/bootloader.bin"
PART_BIN="${OUT_DIR}/partition-table.bin"

if [[ ! -f "${APP_BIN}" ]]; then
  APP_BIN="${BUILD_DIR}/virtual_badge_esp32.bin"
fi
if [[ ! -f "${BOOT_BIN}" ]]; then
  BOOT_BIN="${BUILD_DIR}/bootloader/bootloader.bin"
fi
if [[ ! -f "${PART_BIN}" ]]; then
  PART_BIN="${BUILD_DIR}/partition_table/partition-table.bin"
fi

if [[ ! -f "${APP_BIN}" || ! -f "${BOOT_BIN}" || ! -f "${PART_BIN}" ]]; then
  echo "Missing binaries. Run: ./Build -t esp32"
  exit 1
fi

if [[ -z "${ESPTOOL}" ]]; then
  if command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL="esptool.py"
  elif command -v python3 >/dev/null 2>&1; then
    ESPTOOL="python3 -m esptool"
  else
    echo "esptool not found. Source ESP-IDF export.sh or install esptool."
    exit 1
  fi
fi

${ESPTOOL} --chip esp32 --port "${PORT}" --baud "${BAUD}" write_flash -z \
  0x1000 "${BOOT_BIN}" \
  0x8000 "${PART_BIN}" \
  0x10000 "${APP_BIN}"
