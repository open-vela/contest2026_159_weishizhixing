#!/usr/bin/env bash
# 同步 fb0 屏显 → 重编 jianwei_ui → 出镜像 → 重试烧录 → 测屏
set -euo pipefail
export PATH="$HOME/.local/bin:/home/ubuntu/vela-opensource/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
PORT=/dev/ttyACM0
NUTTX=/home/ubuntu/vela-opensource/nuttx
SRC=/home/ubuntu/jianwei-id/app/jianwei
DST=/home/ubuntu/contest2026_159_weishizhixing_work/app/jianwei

echo "=== sync ==="
cp -f "$SRC/jianwei_ui.c" "$DST/"
grep -n 'fb0\|pick_bg\|FBIO_UPDATE' "$DST/jianwei_ui.c" | head -5

echo "=== rebuild jianwei_ui only ==="
rm -f /home/ubuntu/vela-opensource/apps/packages/demos/contest2026_159_jianwei/jianwei_ui*.o \
      /home/ubuntu/vela-opensource/apps/packages/demos/contest2026_159_jianwei/.built
cd "$NUTTX"
set +e
make EXTRAFLAGS=-Wno-cpp -j"$(nproc)" >/tmp/rebuild-fb-ui2.log 2>&1
rc=$?
set -e
echo "make_rc=$rc (MKIMAGE esptool 版本报错可忽略)"
tail -12 /tmp/rebuild-fb-ui2.log | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' | tr -d '\r'
[[ -f "$NUTTX/nuttx" ]] || { grep -E 'error:|undefined' /tmp/rebuild-fb-ui2.log | tail -40; exit 1; }

echo "=== strings check ==="
if ! strings "$NUTTX/nuttx" | grep -F 'fb0' | head -5; then
  echo "ELF 仍无 fb0，编译可能没吃到新 jianwei_ui.c"
  ls -l "$DST/jianwei_ui.c"
  exit 1
fi

echo "=== elf2image ==="
esptool.py -c esp32s3 elf2image --ram-only-header --dont-append-digest \
  -fs 4MB -fm dio -ff 40m -o "$NUTTX/nuttx.bin" "$NUTTX/nuttx"
ls -lh "$NUTTX/nuttx.bin"

echo "=== flash (retry) ==="
flash_ok=0
for attempt in 1 2 3 4 5; do
  echo "--- attempt $attempt ---"
  for i in $(seq 1 20); do [[ -e "$PORT" ]] && break; sleep 0.4; done
  sleep 1
  if sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
    --before usb_reset --after hard_reset --no-stub \
    write_flash --flash_size keep -fm dio -ff 40m \
    0x0 "$NUTTX/nuttx.bin"; then
    flash_ok=1
    break
  fi
  echo "失败：可按住 BOOT 再试，或重插 USB"
  sleep 3
done
[[ "$flash_ok" == 1 ]] || exit 1

sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" \
  --before default_reset --after hard_reset chip_id >/dev/null 2>&1 || true
for i in $(seq 1 30); do [[ -e "$PORT" ]] && break; sleep 0.5; done
sleep 5

sudo python3 - <<'PY'
import serial, time, glob

def open_port():
    for _ in range(30):
        for p in sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")):
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
            break
        if c:
            b += c
    return b.decode("utf-8", "replace")

def cmd(line, wait=3):
    try:
        s.write((line + "\r\n").encode()); s.flush()
    except Exception as e:
        print("wr", e)
        return ""
    out = rd(wait)
    print(">>>", line)
    print(out[-1200:] if out.strip() else "(empty)")
    return out

print("boot:", rd(5)[-400:])
s.write(b"\r\n"); rd(0.5)
cmd("ls /dev", 2)
out = cmd("jianwei &", 3)
print("==== 12s 短按 BOOT：看屏幕 VERIFY/GUARD；串口应有 fb0 240x240 ====")
buf = rd(12)
print(buf)
alltxt = out + buf
print("结果:", "fb0 OK" if "fb0" in alltxt else "fb0 未起来 —— 把输出发我")
s.close()
PY
