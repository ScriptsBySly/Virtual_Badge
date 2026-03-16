#!/usr/bin/env bash
set -euo pipefail

INPUT_PATH=${1:-}
if [[ -z "${INPUT_PATH}" ]]; then
  echo "Usage: $0 path/to/firmware.(hex|elf)"
  exit 1
fi

MCU=${MCU:-atmega328p}
PORT=${PORT:-/dev/ttyUSB0}
PROGRAMMER=${PROGRAMMER:-arduino}
BAUD=${BAUD:-57600}
AVRDUDE=${AVRDUDE:-avrdude}
OBJCOPY=${OBJCOPY:-avr-objcopy}
STTY=${STTY:-stty}
DTR_RESET=${DTR_RESET:-0}

FLASH_PATH=${INPUT_PATH}
FLASH_FMT=i
TMP_HEX=""

if [[ "${INPUT_PATH}" == *.elf ]]; then
  TMP_HEX=$(mktemp -t avr_flash_XXXXXX.hex)
  "${OBJCOPY}" -O ihex -R .eeprom "${INPUT_PATH}" "${TMP_HEX}"
  FLASH_PATH=${TMP_HEX}
  FLASH_FMT=i
fi

trap '[[ -n "${TMP_HEX}" ]] && rm -f "${TMP_HEX}"' EXIT

if [[ "${DTR_RESET}" == "1" ]]; then
  # Toggle DTR/RTS to force auto-reset on CH340-based boards.
  "${STTY}" -F "${PORT}" 1200 hupcl || true
  sleep 0.1
  "${STTY}" -F "${PORT}" -hupcl || true
  sleep 0.3
fi

"${AVRDUDE}" -p "${MCU}" -c "${PROGRAMMER}" -P "${PORT}" -b "${BAUD}" -D -U "flash:w:${FLASH_PATH}:${FLASH_FMT}"
