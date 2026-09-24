#!/usr/bin/env bash
# 重编（去掉按键时同步心跳）+ 烧录 + 串口自动重连监听
set -euo pipefail
export PATH="$HOME/.local/bin:/home/ubuntu/vela-opensource/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
PORT=/dev/ttyACM0
NUTTX=/home/ubuntu/vela-opensource/nuttx
SRC=/home/ubuntu/jianwei-id/app/jianwei
DST=/home/ubuntu/contest2026_159_weishizhixing_work/app/jianwei

cp -f "$SRC/jianwei_main.c" "$SRC/jianwei_ui.c" "$DST/"
rm -f /home/ubuntu/vela-opensource/apps/packages/demos/contest2026_159_jianwei/jianwei_main*.o \
      /home/ubuntu/vela-opensource/apps/packages/demos/contest2026_159_jianwei/jianwei_ui*.o \
      /home/ubuntu/vela-opensource/apps/packages/demos/contest2026_159_jianwei/.built

cd "$NUTTX"
set +e
make EXTRAFLAGS=-Wno-cpp -j"$(nproc)" >/tmp/rebuild-boot-usb.log 2>&1
rc=$?
set -e
echo "make_rc=$rc"
tail -8 /tmp/rebuild-boot-usb.log | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' | tr -d '\r'
[[ -f nuttx ]] || exit 1
strings nuttx | grep -E 'VERIFY|READY' | head -3

esptool.py -c esp32s3 elf2image --ram-only-header --dont-append-digest \
  -fs 4MB -fm dio -ff 40m -o nuttx.bin nuttx

sudo fuser -k "$PORT" 2>/dev/null || true
sleep 1
flash_ok=0
for a in 1 2 3 4 5; do
  echo "flash $a"
  [[ -e $PORT ]] || sleep 2
  if sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
    --before usb_reset --after hard_reset --no-stub \
    write_flash --flash_size keep -fm dio -ff 40m 0x0 nuttx.bin; then
    flash_ok=1; break
  fi
  sleep 3
done
[[ $flash_ok == 1 ]] || exit 1
sleep 5

sudo python3 - <<'PY'
import serial, time, glob, sys

def open_port():
    while True:
        for p in sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")):
            try:
                s = serial.Serial(p, 115200, timeout=0.25)
                s.dtr = False
                s.rts = False
                print("\n[opened]", p, flush=True)
                return s
            except Exception as e:
                print("[open fail]", p, e, flush=True)
        time.sleep(0.5)

def pump(s, sec, prefix_ok=True):
    t = time.time()
    while time.time() - t < sec:
        try:
            c = s.read(4096)
        except Exception as e:
            print("\n[串口掉线]", e, flush=True)
            return False
        if c and prefix_ok:
            sys.stdout.write(c.decode("utf-8", "replace"))
            sys.stdout.flush()
    return True

s = open_port()
print("===== boot =====", flush=True)
if not pump(s, 5):
    try: s.close()
    except Exception: pass
    s = open_port(); pump(s, 3)

for _ in range(8):
    try:
        s.write(b"\r\n"); s.flush()
    except Exception:
        s = open_port()
    time.sleep(0.2)
pump(s, 1)

try:
    s.write(b"pkill jianwei\r\n"); s.flush()
except Exception:
    s = open_port()
pump(s, 1)
try:
    s.write(b"jianwei &\r\n"); s.flush()
except Exception:
    s = open_port()
pump(s, 3)

print("""
========================================
窗口保持打开。短按 BOOT：
  终端应出现 mode -> verify / guard
  小屏大字 VERIFY READY / GUARD READY
若提示串口掉线会自动重连。Ctrl+C 结束。
========================================
""", flush=True)

try:
    while True:
        ok = pump(s, 1.0)
        if not ok:
            try:
                s.close()
            except Exception:
                pass
            time.sleep(1)
            s = open_port()
            try:
                s.write(b"\r\njianwei &\r\n"); s.flush()
            except Exception:
                pass
            pump(s, 2)
except KeyboardInterrupt:
    print("\nbye", flush=True)
finally:
    try:
        s.dtr = False; s.rts = False; s.close()
    except Exception:
        pass
PY
