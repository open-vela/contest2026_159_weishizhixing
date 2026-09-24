#!/usr/bin/env bash
# 仅重试烧录 + 启动 jianwei + 验心跳（固件已编好时用）
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
PORT=/dev/ttyACM0
BIN=/home/ubuntu/vela-opensource/nuttx/nuttx.bin
VM_IP=$(hostname -I | awk '{print $1}')

echo "VM_IP=$VM_IP  BIN=$BIN"
ls -l "$PORT" "$BIN"

flash_ok=0
for attempt in 1 2 3; do
  echo "=== flash attempt $attempt ==="
  if [[ "$attempt" -le 2 ]]; then
    if sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
      --before usb_reset --after hard_reset --no-stub \
      write_flash --flash_size keep -fm dio -ff 40m \
      0x0 "$BIN"; then
      flash_ok=1; break
    fi
  else
    echo "这次请按住 BOOT，看到 Wrote 再松开"
    if sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
      --before default_reset --after hard_reset \
      write_flash -z --flash_size keep -fm dio -ff 40m \
      0x0 "$BIN"; then
      flash_ok=1; break
    fi
  fi
  sleep 3
done
[[ "$flash_ok" == 1 ]] || { echo FLASH_FAIL; exit 1; }

sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" \
  --before default_reset --after hard_reset chip_id >/dev/null 2>&1 || true
sleep 6

# 确保云端在
if ! curl -sf -o /dev/null http://127.0.0.1:8787/; then
  cd /home/ubuntu/jianwei-id/cloud
  # shellcheck disable=SC1091
  source /home/ubuntu/jianwei-id/.venv/bin/activate 2>/dev/null || true
  nohup python3 -u app.py > /tmp/jianwei-cloud.log 2>&1 &
  sleep 2
fi

sudo python3 - <<PY
import serial, time, urllib.request, re
port = "$PORT"
vm = "$VM_IP"
s = serial.Serial(port, 115200, timeout=0.3)
try:
    s.dtr = False; s.rts = True; time.sleep(0.05); s.rts = False
except Exception:
    pass

def rd(sec):
    b = b""; t = time.time()
    while time.time() - t < sec:
        c = s.read(2048)
        if c: b += c
    return b.decode("utf-8", "replace")

def cmd(line, wait=3):
    s.write((line + "\r\n").encode()); s.flush()
    out = rd(wait)
    print(">>>", line); print(out[-900:]); return out

print("boot:", rd(4)[-500:])
s.write(b"\r\n"); rd(0.5)
for _ in range(10):
    out = cmd("ifconfig wlan0", 2)
    if re.search(r"inet addr:[0-9.]+", out) and "RUNNING" in out:
        break
    time.sleep(2)
cmd(f"ping -c 3 {vm}", 8)
cmd("jianwei", 3)
print("--- beats ---"); print(rd(14)[-1200:])
s.close()
time.sleep(1)
print("DEVICE", urllib.request.urlopen("http://127.0.0.1:8787/api/device", timeout=3).read().decode())
print("看板 http://%s:8787" % vm)
PY
