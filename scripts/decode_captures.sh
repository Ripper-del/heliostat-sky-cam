#!/usr/bin/env bash
# Запускається в Docker: make decode  (sigrok-cli є в образі; на macOS brew його не ставить)
# Декодує записи docs/captures/*.sr у текст (анотації sigrok) -> docs/captures/<name>.txt
#   scripts/decode_captures.sh            # усі записи
#   scripts/decode_captures.sh <name>     # один запис
# Канали: D0 = SCL/SCK/RX/data, D1 = SDA/MISO, D2 = MOSI, D3 = CS# (див. розбір по префіксу імені нижче).
# Декодер вибирається за префіксом імені: i2c_*, sccb_*, spi_*, uart_*, pwm_*.
set -euo pipefail
# Вхід: .sr з docs/captures (або .vcd з scripts/fixtures, якщо задано CAPDIR).
cd "${CAPDIR:-$(dirname "$0")/../docs/captures}"
command -v sigrok-cli >/dev/null || { echo "sigrok-cli не знайдено (brew install sigrok-cli)"; exit 1; }

decode() {
  local f=$1 base; base=$(basename "$f"); base=${base%.*}
  case "$base" in
    i2c_*|sccb_*) P="i2c:scl=D0:sda=D1"; A="i2c=start:repeat-start:stop:address-read:address-write:data-read:data-write:ack:nack:warnings" ;;                     # SCCB читається тим самим декодером
    spi_*)        P="spi:clk=D0:miso=D1:mosi=D2:cs=D3:cpol=0:cpha=0:wordsize=8"; A="spi=mosi-data:miso-data:warnings" ;;
    uart_*)       P="uart:rx=D0:baudrate=115200"; A="uart=rx-data" ;;
    pwm_*)        P="pwm:data=D0"; A="pwm" ;;
    *)            echo "  пропущено $base (невідомий префікс)"; return ;;
  esac
  if [ "$base" != "${base#spi_}" ]; then   # SPI: MOSI і MISO окремо (інакше в тексті не видно, чиї це байти)
    { sigrok-cli -i "$f" -P "$P" -A "spi=mosi-data" | sed 's/^spi-1: /MOSI /';
      sigrok-cli -i "$f" -P "$P" -A "spi=miso-data" | sed 's/^spi-1: /MISO /'; } > "$base.txt"
  else
    sigrok-cli -i "$f" -P "$P" -A "$A" > "$base.txt"
  fi
  echo "  $base: $(wc -l < "$base.txt") рядків анотацій -> $base.txt"
}

EXT=${EXT:-sr}
if [ $# -ge 1 ]; then decode "$1.$EXT"; else for f in *."$EXT"; do [ -e "$f" ] && decode "$f"; done; fi
