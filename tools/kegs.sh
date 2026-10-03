#!/bin/bash
# kegs.sh - drive KEGS (the IIgs emulator, ~/repos/kegs.1.38) headless for
# the live tests, on Xvfb :99 (WSLg cannot deliver injected keys).
#
#   kegs.sh start DISK.po [SPEED]   power on with DISK as s7d1 (SmartPort);
#                                   SPEED: 1 = 1 MHz, 2 = 2.8 MHz (default), 0 = unlimited
#   kegs.sh type TEXT               keys, as typed ('\n' = Return)
#   kegs.sh key KEYSYM...           xdotool key names (Return, Escape, ctrl+c)
#   kegs.sh shot FILE.png           the window, as a PNG
#   kegs.sh text                    the text screen, as text (kegs_text.py)
#   kegs.sh stop
set -eu
DISP=:99
KEGS=$HOME/repos/kegs.1.38
ROM=$HOME/repos/strudelworks-host/assets/apple2gs_rom3.bin
RUN=${KEGS_RUN:-/tmp/as816-kegs}
export DISPLAY=$DISP

win() { xdotool search --name KEGS 2>/dev/null | head -1; }

case ${1:-} in
start)
  disk=$(realpath "$2")
  speed=${3:-2}
  mkdir -p "$RUN"
  if ! pgrep -f "^Xvfb $DISP" >/dev/null; then
    Xvfb $DISP -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
    sleep 1
  fi
  pid=$(pgrep -x xkegs || true)
  [ -n "$pid" ] && kill $pid && sleep 0.5
  cat > "$RUN/config.kegs" <<EOF
g_cfg_rom_path = $ROM
s7d1 = $disk
g_limit_speed = $speed
g_config_kegs_auto_update = 0
EOF
  (cd "$RUN" && nohup "$KEGS/xkegs" -display $DISP -cfg "$RUN/config.kegs" >"$RUN/kegs.log" 2>&1 &)
  for _ in $(seq 50); do [ -n "$(win)" ] && break; sleep 0.1; done
  w=$(win)
  [ -n "$w" ] || { echo "kegs.sh: no KEGS window" >&2; exit 1; }
  # keys go to the window under the pointer
  eval "$(xdotool getwindowgeometry --shell "$w")"
  xdotool mousemove $((X + WIDTH / 2)) $((Y + HEIGHT / 2))
  echo "kegs.sh: KEGS up (window $w)"
  ;;
type)
  # KEGS takes Return only as a key event, not as typed text
  rest=$2
  while [ -n "$rest" ]; do
    part=${rest%%$'\n'*}
    [ -n "$part" ] && xdotool type --delay 60 -- "$part"
    [ "$part" = "$rest" ] && break
    xdotool key Return
    rest=${rest#*$'\n'}
  done
  ;;
key)
  shift
  xdotool key --delay 80 "$@"
  ;;
shot)
  w=$(win)
  xwd -silent -id "$w" | python3 -c '
import sys, struct
from PIL import Image
d = sys.stdin.buffer.read()
hs, = struct.unpack(">I", d[0:4])
w, h = struct.unpack(">II", d[16:24])
bpl, = struct.unpack(">I", d[48:52])
ncolors, = struct.unpack(">I", d[76:80])
off = hs + ncolors * 12
Image.frombuffer("RGB", (w, h), d[off:off + bpl * h], "raw", "BGRX", bpl, 1).save(sys.argv[1])
' "$2"
  ;;
text)
  t=$(mktemp --suffix=.png)
  "$0" shot "$t"
  python3 "$(dirname "$0")/kegs_text.py" "$t"
  rm -f "$t"
  ;;
stop)
  pid=$(pgrep -x xkegs || true)
  [ -n "$pid" ] && kill $pid
  ;;
*)
  sed -n '2,13p' "$0"
  exit 2
  ;;
esac
