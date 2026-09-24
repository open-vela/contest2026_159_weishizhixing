#!/usr/bin/env bash
# 强力恢复：杀占口进程 → 多次复位 → 等 nsh → 起 jianwei
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"

echo "=== A. 杀掉占用串口的进程 ==="
sudo fuser -k /dev/ttyACM0 2>/dev/null || true
sudo fuser -k /dev/ttyUSB0 2>/dev/null || true
pkill -f 'serial.Serial' 2>/dev/null || true
pkill -f 'minicom|screen|picocom|jianwei.*tty' 2>/dev/null || true
sleep 1

echo "=== B. 看 USB 设备 ==="
lsusb | grep -i espressif || echo "没有 Espressif：请重插 USB，并在 VMware 里把板子勾给这台虚拟机"
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "暂无串口节点"

if ! ls /dev/ttyACM* >/dev/null 2>&1; then
  echo ""
  echo "请现在：1) 拔掉板子 USB  2) 等 3 秒  3) 再插上"
  echo "插好后按回车继续..."
  read -r _
  sleep 2
fi

PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1 || true)
if [[ -z "${PORT:-}" ]]; then
  echo "仍无 /dev/ttyACM*，无法继续"
  lsusb
  exit 1
fi
echo "使用端口: $PORT"

echo "=== C. 复位尝试 1: hard_reset ==="
sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
  --before default_reset --after hard_reset chip_id 2>&1 | tail -20 || true
sleep 3

PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1 || true)
echo "=== C2. 复位尝试 2: usb_reset ==="
if [[ -n "${PORT:-}" ]]; then
  sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
    --before usb_reset --after hard_reset chip_id 2>&1 | tail -20 || true
fi
sleep 4

PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1 || true)
[[ -n "${PORT:-}" ]] || { echo "复位后串口消失，请重插 USB 再跑"; exit 1; }
echo "串口: $PORT"

echo "=== D. 读启动日志 / 找 nsh ==="
sudo python3 - <<PY
import serial, time, sys
port = "$PORT"
s = serial.Serial(port, 115200, timeout=0.4)
# 先别乱抖 DTR；给一点启动时间
time.sleep(0.2)

def rd(sec):
    b = b""; t = time.time()
    while time.time() - t < sec:
        try:
            c = s.read(4096)
        except Exception as e:
            print("rd", e); break
        if c: b += c
    return b.decode("utf-8", "replace")

print("--- raw 8s ---")
print(rd(8)[-2000:])

# 软唤醒
for i in range(15):
    s.write(b"\r\n"); s.flush(); time.sleep(0.2)
wake = rd(3)
print("--- wake ---")
print(repr(wake[-300:]))
print(wake[-800:])

if "nsh>" not in wake and "NuttShell" not in wake and "NuttX" not in wake:
    print("STILL_NO_NSH")
    # 最后手段：用 DTR/RTS 脉冲复位一次
    try:
        s.dtr = False
        s.rts = True
        time.sleep(0.1)
        s.rts = False
        time.sleep(2)
        print("--- after rts pulse ---")
        print(rd(6)[-1500:])
        s.write(b"\r\n"); print(rd(2))
    except Exception as e:
        print("rts fail", e)
    s.close()
    sys.exit(2)

def cmd(line, wait=3):
    s.write((line + "\r\n").encode()); s.flush()
    out = rd(wait)
    print(">>>", line)
    print(out[-1000:] if out.strip() else "(empty)")
    return out

cmd("uname -a", 3)
cmd("pkill jianwei", 1)
out = cmd("jianwei &", 3)
print("==== 按 BOOT：应出现 mode -> / 屏切换；听 40s ====")
print(rd(40))
try:
    s.dtr = False; s.rts = False; time.sleep(0.05); s.close()
except Exception:
    pass
print("DONE")
PY
