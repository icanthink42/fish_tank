#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_NAME="${PIO_ENV:-waveshare_esp32s3_touch_amoled_216}"
PIO_CMD=""

require_platformio() {
  if [[ -x "$PROJECT_ROOT/.venv/bin/pio" ]]; then
    PIO_CMD="$PROJECT_ROOT/.venv/bin/pio"
    return 0
  fi

  if command -v pio >/dev/null 2>&1; then
    PIO_CMD="pio"
    return 0
  fi

  cat >&2 <<'EOF'
PlatformIO CLI was not found.

Run:
  ./scripts/setup.sh

Then run this script again.
EOF
  exit 127
}

is_serial_device() {
  [[ -e "$1" && ( "$1" == /dev/tty* || "$1" == /dev/cu.* || "$1" == /dev/serial/by-id/* ) ]]
}

detect_port_from_by_id() {
  local match
  shopt -s nullglob
  for match in /dev/serial/by-id/*; do
    if [[ "$match" =~ (Espressif|ESP32|USB_JTAG|JTAG|Waveshare|CP210|CH343|CH910|UART|ACM) ]]; then
      readlink -f "$match"
      return 0
    fi
  done
  return 1
}

detect_port_from_platformio() {
  "$PIO_CMD" device list 2>/dev/null \
    | awk '
        /^\/dev\// { port=$1 }
        /Espressif|ESP32|USB JTAG|JTAG|Waveshare|CP210|CH343|CH910|USB Serial|CDC|ACM/ && port {
          print port
          exit
        }
      '
}

detect_port_from_glob() {
  local candidate
  shopt -s nullglob
  for candidate in \
    /dev/ttyACM* /dev/ttyUSB* \
    /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial*; do
    if is_serial_device "$candidate"; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

detect_port() {
  if [[ -n "${PORT:-}" ]]; then
    if is_serial_device "$PORT"; then
      printf '%s\n' "$PORT"
      return 0
    fi
    printf 'PORT is set but does not exist or is not a serial device: %s\n' "$PORT" >&2
    exit 2
  fi

  detect_port_from_by_id || detect_port_from_platformio || detect_port_from_glob || {
    cat >&2 <<'EOF'
Could not find an ESP32-S3 serial port.

Connect the board over USB-C and try again. If the board is not visible, hold BOOT,
tap RESET or reconnect USB, then release BOOT. You can also pass the port manually:

  PORT=/dev/ttyACM0 ./scripts/flash.sh
EOF
    exit 3
  }
}

main() {
  require_platformio

  cd "$PROJECT_ROOT"
  local port
  port="$(detect_port)"

  printf 'Building PlatformIO environment: %s\n' "$ENV_NAME"
  "$PIO_CMD" run -e "$ENV_NAME"

  printf 'Flashing %s to %s\n' "$ENV_NAME" "$port"
  "$PIO_CMD" run -e "$ENV_NAME" -t upload --upload-port "$port"

  printf '\nDone. To watch logs:\n  %s device monitor -p %s -b 115200\n' "$PIO_CMD" "$port"
}

main "$@"
