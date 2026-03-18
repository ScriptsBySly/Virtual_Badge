#!/usr/bin/env bash
set -euo pipefail

PORT=${PORT:-/dev/ttyUSB0}
BAUD=${BAUD:-460800}
OUT_DIR=${OUT_DIR:-output}
BUILD_DIR=${BUILD_DIR:-build}
ESPTOOL=${ESPTOOL:-}
CHIP=${CHIP:-esp32}

usage() {
  echo "Usage: $0 [-d /dev/ttyUSB0] [-b 460800] [-o output] [-B build] [-c esp32|esp32s3]"
}

while getopts ":d:b:o:B:c:h" opt; do
  case "${opt}" in
    d) PORT="${OPTARG}" ;;
    b) BAUD="${OPTARG}" ;;
    o) OUT_DIR="${OPTARG}" ;;
    B) BUILD_DIR="${OPTARG}" ;;
    c) CHIP="${OPTARG}" ;;
    h) usage; exit 0 ;;
    *) usage; exit 1 ;;
  esac
done

APP_BIN="${OUT_DIR}/virtual_badge_esp32.bin"
BOOT_BIN="${OUT_DIR}/bootloader.bin"
PART_BIN="${OUT_DIR}/partition-table.bin"
FLASH_ARGS_FILE="${BUILD_DIR}/flash_args"

if [[ ! -f "${APP_BIN}" ]]; then
  APP_BIN="${BUILD_DIR}/virtual_badge_esp32.bin"
fi
if [[ ! -f "${BOOT_BIN}" ]]; then
  BOOT_BIN="${BUILD_DIR}/bootloader/bootloader.bin"
fi
if [[ ! -f "${PART_BIN}" ]]; then
  PART_BIN="${BUILD_DIR}/partition_table/partition-table.bin"
fi

if [[ -f "${FLASH_ARGS_FILE}" ]]; then
  : # Prefer flash_args if available (it encodes correct offsets).
else
  if [[ ! -f "${APP_BIN}" || ! -f "${BOOT_BIN}" || ! -f "${PART_BIN}" ]]; then
    echo "Missing binaries. Run: ./Build -t esp32 or ./Build -t esp32s3"
    exit 1
  fi
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

if [[ -f "${FLASH_ARGS_FILE}" ]]; then
  FLASH_ARGS=()
  while IFS= read -r line; do
    for word in ${line}; do
      FLASH_ARGS+=("${word}")
    done
  done < "${FLASH_ARGS_FILE}"
  (cd "${BUILD_DIR}" && ${ESPTOOL} --chip "${CHIP}" --port "${PORT}" --baud "${BAUD}" write_flash -z "${FLASH_ARGS[@]}")
else
  ${ESPTOOL} --chip "${CHIP}" --port "${PORT}" --baud "${BAUD}" write_flash -z \
    0x1000 "${BOOT_BIN}" \
    0x8000 "${PART_BIN}" \
    0x10000 "${APP_BIN}"
fi
