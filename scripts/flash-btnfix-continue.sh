#!/usr/bin/env bash
# 上次 make 已链出 nuttx ELF，只补镜像+烧录+测按键
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
PORT=/dev/ttyACM0
cd /home/ubuntu/vela-opensource/nuttx

# 确认新代码已进 ELF
if ! strings nuttx | grep -q "BOOT short"; then
  echo "ELF 里没有 BOOT short 字符串，请重新跑 rebuild-flash-btnfix.sh"
  exit 1
fi
echo "ELF contains BOOT short OK"

esptool.py -c esp32s3 elf2image --ram-only-header --dont-append-digest \
  -fs 4MB -fm dio -ff 40m -o nuttx.bin nuttx
ls -lh nuttx.bin

flash_ok=0
for attempt in 1 2 3; do
  echo "--- flash attempt $attempt ---"
  if sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
    --before usb_reset --after hard_reset --no-stub \
    write_flash --flash_size keep -fm dio -ff 40m \
    0x0 nuttx.bin; then
    flash_ok=1; break
  fi
  sleep 2
done
[[ "$flash_ok" == 1 ]] || { echo FLASH_FAIL; exit 1; }

sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" \
  --before default_reset --after hard_reset chip_id >/dev/null 2>&1 || true
sleep 5

sudo python3 - <<'PY'
import serial, time
s = serial.Serial("/dev/ttyACM0", 115200, timeout=0.25)

def rd(sec):
    b = b""; t = time.time()
    while time.time() - t < sec:
        c = s.read(4096)
        if c: b += c
    return b.decode("utf-8", "replace")

def cmd(line, wait=2):
    s.write((line + "\r\n").encode()); s.flush()
    print(rd(wait)[-900:])

print("boot:", rd(5)[-400:])
s.write(b"\r\n"); rd(0.5)
cmd("jianwei &", 2)
print("==== 15s 内短按 BOOT 2～3 次 ====")
buf = rd(15)
print(buf)
ok = ("mode ->" in buf) or ("BOOT down" in buf) or ("BOOT short" in buf)
print("结果:", "按键 OK" if ok else "仍无按键边沿 —— 把输出发我")
s.close()
PY
