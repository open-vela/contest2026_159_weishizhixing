#!/usr/bin/env bash
# 打开 CONFIG_INPUT + buttons，确认符号进 ELF，再烧录；串口断开会自动重连
set -euo pipefail
export PATH="$HOME/.local/bin:/home/ubuntu/vela-opensource/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
PORT=/dev/ttyACM0
NUTTX=/home/ubuntu/vela-opensource/nuttx
SRC=/home/ubuntu/jianwei-id/app/jianwei
DST=/home/ubuntu/contest2026_159_weishizhixing_work/app/jianwei
CFG="$NUTTX/.config"

cp -f "$SRC/jianwei_main.c" "$SRC/jianwei_ui.c" "$SRC/defconfig.append" "$DST/"

echo "=== force-enable INPUT + buttons in .config ==="
python3 - <<'PY'
from pathlib import Path
import re
p = Path("/home/ubuntu/vela-opensource/nuttx/.config")
t = p.read_text()
want = [
    "CONFIG_INPUT=y",
    "CONFIG_ARCH_BUTTONS=y",
    "CONFIG_ARCH_IRQBUTTONS=y",
    "CONFIG_INPUT_BUTTONS=y",
    "CONFIG_INPUT_BUTTONS_LOWER=y",
]
for b in want:
    key = b.split("=")[0]
    # remove "is not set" and any existing assignment
    t = re.sub(rf'^# {re.escape(key)} is not set\s*\n', '', t, flags=re.M)
    t = re.sub(rf'^{re.escape(key)}=.*\n', '', t, flags=re.M)
    t += b + "\n"
    print("force", b)
p.write_text(t)
PY

cd "$NUTTX"
make olddefconfig >/tmp/olddef-input.log 2>&1 || true

echo "=== verify .config after olddefconfig ==="
grep -E '^CONFIG_INPUT=|^CONFIG_ARCH_BUTTONS=|^CONFIG_ARCH_IRQBUTTONS=|^CONFIG_INPUT_BUTTONS' "$CFG" || {
  echo "CONFIG lost after olddefconfig:"; grep -E 'INPUT|BUTTONS' "$CFG" | head -30; exit 1;
}

# Sync config.h
make include/nuttx/config.h >/dev/null 2>&1 || make -C "$NUTTX" 2>/dev/null | true
grep -E 'CONFIG_INPUT |CONFIG_INPUT_BUTTONS|CONFIG_ARCH_BUTTONS' "$NUTTX/include/nuttx/config.h" | head

echo "=== rebuild drivers/boards/jianwei (keep libapps bulk) ==="
rm -f "$NUTTX"/drivers/input/button_*.o \
      "$NUTTX"/drivers/libdrivers.a \
      "$NUTTX"/staging/libdrivers.a \
      "$NUTTX"/boards/xtensa/esp32s3/esp32s3-eye/src/*.o \
      "$NUTTX"/boards/xtensa/esp32s3/common/esp32s3_buttons*.o \
      "$NUTTX"/boards/libboards.a \
      "$NUTTX"/staging/libboards.a \
      "$NUTTX"/arch/xtensa/src/board/libboard.a \
      /home/ubuntu/vela-opensource/apps/packages/demos/contest2026_159_jianwei/jianwei_*.o \
      /home/ubuntu/vela-opensource/apps/packages/demos/contest2026_159_jianwei/.built

# Touch bringup so CONFIG_INPUT_BUTTONS path recompiles
touch "$NUTTX/boards/xtensa/esp32s3/esp32s3-eye/src/esp32s3_bringup.c" \
      "$NUTTX/boards/xtensa/esp32s3/esp32s3-eye/src/esp32s3_buttons.c" \
      "$NUTTX/drivers/input/button_lower.c" \
      "$NUTTX/drivers/input/button_upper.c"

set +e
make EXTRAFLAGS=-Wno-cpp -j"$(nproc)" >/tmp/rebuild-input-btn.log 2>&1
make_rc=$?
set -e
echo "make_rc=$make_rc"
tail -15 /tmp/rebuild-input-btn.log | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' | tr -d '\r'
[[ -f "$NUTTX/nuttx" ]] || { grep -E 'error:|undefined' /tmp/rebuild-input-btn.log | tail -40; exit 1; }

echo "=== must have button symbols ==="
nm "$NUTTX/nuttx" | grep -E 'btn_lower_initialize|board_buttons|jianwei_main' || {
  echo "button symbols missing in ELF"
  nm "$NUTTX/staging/libdrivers.a" 2>/dev/null | grep btn_ | head
  nm "$NUTTX/staging/libboards.a" 2>/dev/null | grep button | head
  exit 1
}

esptool.py -c esp32s3 elf2image --ram-only-header --dont-append-digest \
  -fs 4MB -fm dio -ff 40m -o "$NUTTX/nuttx.bin" "$NUTTX/nuttx"

echo "=== flash ==="
sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
  --before usb_reset --after hard_reset --no-stub \
  write_flash --flash_size keep -fm dio -ff 40m \
  0x0 "$NUTTX/nuttx.bin"

echo "=== wait USB serial reappear ==="
for i in $(seq 1 30); do
  if [[ -e "$PORT" ]]; then echo "port ready ($i)"; break; fi
  sleep 0.5
done
sleep 3

sudo python3 - <<'PY'
import serial, time, glob, os

def open_port():
    for _ in range(20):
        ports = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
        for p in ports:
            try:
                s = serial.Serial(p, 115200, timeout=0.25)
                print("opened", p)
                return s
            except Exception as e:
                print("open fail", p, e)
        time.sleep(0.5)
    raise SystemExit("no serial")

s = open_port()

def rd(sec):
    b = b""; t = time.time()
    while time.time() - t < sec:
        try:
            c = s.read(4096)
        except Exception as e:
            print("rd", e)
            time.sleep(0.2)
            break
        if c: b += c
    return b.decode("utf-8", "replace")

def cmd(line, wait=2.5):
    try:
        s.write((line + "\r\n").encode()); s.flush()
    except Exception as e:
        print("write fail, reopen", e)
        return ""
    out = rd(wait)
    print(">>>", line)
    print(out[-1200:] if out.strip() else "(empty)")
    return out

print("boot:", rd(4)[-500:])
s.write(b"\r\n"); rd(0.5)
cmd("ls /dev", 3)
out = cmd("jianwei &", 2)
if "open /dev/buttons failed" in out:
    print("FAIL: buttons still missing")
print("==== 15s 短按 BOOT 2～3 次（不是 RST）====")
buf = rd(15)
print(buf)
ok = ("mode ->" in buf) or ("BOOT down" in buf) or ("BOOT short" in buf)
bad = "open /dev/buttons failed" in buf
print("结果:", "按键 OK" if ok and not bad else "失败 —— 把输出发我")
try:
    s.close()
except Exception:
    pass
PY
