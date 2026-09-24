#!/usr/bin/env bash
# 见微：起云端 + 查 IP + 必要时重烧 + 启动 jianwei + 验心跳
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"

BIN=/home/ubuntu/vela-opensource/nuttx/nuttx.bin
CLOUD_DIR=/home/ubuntu/jianwei-id/cloud
PORT=/dev/ttyACM0

echo "=== 1. 串口 ==="
if [[ ! -e "$PORT" ]]; then
  echo "找不到 $PORT。请确认：VMware 已把板子勾给这台虚拟机，然后重插 USB。"
  lsusb | grep -i espressif || true
  exit 1
fi
ls -l "$PORT"

echo "=== 2. 本机 IP（桥接应是 10.x）==="
VM_IP=$(hostname -I | awk '{print $1}')
echo "VM_IP=$VM_IP"
ip -4 addr show | grep -E 'inet ' || true
if [[ -z "${VM_IP:-}" || "$VM_IP" == 127.* || "$VM_IP" == 192.168.80.* || "$VM_IP" == 192.168.91.* ]]; then
  echo "警告：当前不像桥接热点网段。若板子连不上云端，请把 VMware 网卡改成 Bridged。"
fi

echo "=== 3. 启动云端 :8787 ==="
pkill -f 'python3 -u app.py' 2>/dev/null || true
pkill -f 'python3 app.py' 2>/dev/null || true
sleep 1
cd "$CLOUD_DIR"
if [[ -f /home/ubuntu/jianwei-id/.venv/bin/activate ]]; then
  # shellcheck disable=SC1091
  source /home/ubuntu/jianwei-id/.venv/bin/activate
fi
nohup python3 -u app.py > /tmp/jianwei-cloud.log 2>&1 &
for i in $(seq 1 20); do
  if curl -sf -o /dev/null http://127.0.0.1:8787/; then
    echo "云端 OK http://$VM_IP:8787"
    break
  fi
  sleep 0.5
done
curl -sf -o /dev/null http://127.0.0.1:8787/ || {
  echo "云端启动失败，看 /tmp/jianwei-cloud.log"
  tail -30 /tmp/jianwei-cloud.log || true
  exit 1
}

echo "=== 4. 固件里的云端地址 ==="
HOST_IN_FW=$(strings "$BIN" 2>/dev/null | grep -E '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$' | head -5 || true)
echo "bin hosts sample: $HOST_IN_FW"
NEED_FLASH=0
if strings "$BIN" | grep -F -q -- "$VM_IP"; then
  echo "固件已含 $VM_IP"
  NEED_FLASH=0
else
  echo "固件未包含当前 VM_IP=$VM_IP，需要重编重烧。"
  NEED_FLASH=1
fi

if [[ "$NEED_FLASH" == 1 ]]; then
  echo "=== 4b. 更新 CLOUD_HOST 并重编 ==="
  sed -i "s/^CONFIG_JIANWEI_CLOUD_HOST=.*/CONFIG_JIANWEI_CLOUD_HOST=\"$VM_IP\"/" \
    /home/ubuntu/vela-opensource/nuttx/.config || true
  for f in /home/ubuntu/jianwei-id/app/jianwei/defconfig.append \
           /home/ubuntu/contest2026_159_weishizhixing_work/app/jianwei/defconfig.append; do
    [[ -f "$f" ]] && sed -i "s/^CONFIG_JIANWEI_CLOUD_HOST=.*/CONFIG_JIANWEI_CLOUD_HOST=\"$VM_IP\"/" "$f"
  done
  export PATH="/home/ubuntu/vela-opensource/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
  rm -f /home/ubuntu/vela-opensource/apps/packages/demos/contest2026_159_jianwei/*.o
  cd /home/ubuntu/vela-opensource/nuttx
  make EXTRAFLAGS=-Wno-cpp -j"$(nproc)" >/tmp/rebuild-bringup.log 2>&1 || true
  esptool.py -c esp32s3 elf2image --ram-only-header --dont-append-digest \
    -fs 4MB -fm dio -ff 40m -o nuttx.bin nuttx
  echo "=== 4c. 烧录（Connecting 时可按住 BOOT；失败会自动降速重试）==="
  flash_ok=0
  for attempt in 1 2 3; do
    echo "--- flash attempt $attempt ---"
    if [[ "$attempt" == 1 ]]; then
      if sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
        --before usb_reset --after hard_reset --no-stub \
        write_flash --flash_size keep -fm dio -ff 40m \
        0x0 nuttx.bin; then
        flash_ok=1; break
      fi
    elif [[ "$attempt" == 2 ]]; then
      if sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
        --before default_reset --after hard_reset \
        write_flash -z --flash_size keep -fm dio -ff 40m \
        0x0 nuttx.bin; then
        flash_ok=1; break
      fi
    else
      echo "请按住板上 BOOT，松手时机：开始写入后"
      if sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 460800 \
        --before usb_reset --after hard_reset \
        write_flash -z --flash_size keep -fm dio -ff 40m \
        0x0 nuttx.bin; then
        flash_ok=1; break
      fi
    fi
    sleep 3
  done
  if [[ "$flash_ok" != 1 ]]; then
    echo "烧录失败。可手动：按住 BOOT 再执行 scripts/flash-retry.sh"
    exit 1
  fi
  # 退出下载模式
  sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" \
    --before default_reset --after hard_reset chip_id >/dev/null 2>&1 || true
  sleep 5
fi

echo "=== 5. 串口：WiFi + jianwei + 心跳 ==="
sudo python3 - <<PY
import serial, time, urllib.request, re, sys
port = "$PORT"
vm = "$VM_IP"
s = serial.Serial(port, 115200, timeout=0.3)
try:
    s.dtr = False
    s.rts = True
    time.sleep(0.05)
    s.rts = False
except Exception:
    pass

def rd(sec):
    b = b""
    t = time.time()
    while time.time() - t < sec:
        c = s.read(2048)
        if c:
            b += c
    return b.decode("utf-8", "replace")

def cmd(line, wait=3):
    s.write((line + "\r\n").encode())
    s.flush()
    out = rd(wait)
    print(">>>", line)
    print(out[-900:])
    return out

print("boot:", rd(3)[-400:])
s.write(b"\r\n"); rd(0.5)
board_ip = None
for _ in range(10):
    out = cmd("ifconfig wlan0", 2)
    m = re.search(r"inet addr:([0-9.]+)", out)
    if m and "RUNNING" in out:
        board_ip = m.group(1)
        print("BOARD_IP", board_ip)
        break
    time.sleep(2)
if not board_ip:
    print("WiFi 未拿到 IP：确认热点 mfl 开着，板子能连上")
cmd(f"ping -c 3 {vm}", 8)
cmd("jianwei", 3)
print("--- beats ---")
print(rd(14)[-1200:])
s.close()
time.sleep(1)
try:
    body = urllib.request.urlopen("http://127.0.0.1:8787/api/device", timeout=3).read().decode()
    print("DEVICE", body)
except Exception as e:
    print("api fail", e)
    sys.exit(2)
PY

echo "=== 完成 ==="
echo "看板: http://$VM_IP:8787"
echo "短按切模式，长按求助。录视频时串口敲: nsh> jianwei"
