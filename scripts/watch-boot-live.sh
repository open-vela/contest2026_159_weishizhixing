#!/usr/bin/env bash
# 保持串口一直开着：复位→起 jianwei→你按 BOOT 时终端会刷 mode ->
# 不要关这个窗口，关了板子可能被复位、jianwei 就没了
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"

sudo fuser -k /dev/ttyACM0 2>/dev/null || true
sleep 1

PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1 || true)
if [[ -z "${PORT:-}" ]]; then
  echo "没有串口。请拔插 USB 后再跑。"
  exit 1
fi

echo "复位板子..."
sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" \
  --before default_reset --after hard_reset chip_id >/dev/null 2>&1 || true
sleep 5
PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1)
echo "串口 $PORT"

sudo python3 - <<PY
import serial, time, sys

port = "$PORT"
s = serial.Serial(port, 115200, timeout=0.2)
s.dtr = False
s.rts = False

def rd(sec):
    b = b""; t = time.time()
    while time.time() - t < sec:
        try:
            c = s.read(4096)
        except Exception as e:
            print("\\n[串口读错误]", e, flush=True)
            time.sleep(0.3)
            return b.decode("utf-8", "replace")
        if c:
            b += c
            sys.stdout.write(c.decode("utf-8", "replace"))
            sys.stdout.flush()
    return b.decode("utf-8", "replace")

print("===== 等待启动 =====", flush=True)
rd(5)
for _ in range(10):
    s.write(b"\\r\\n"); s.flush(); time.sleep(0.2)
rd(1)

print("\\n===== 启动 jianwei =====", flush=True)
s.write(b"pkill jianwei\\r\\n"); rd(1)
s.write(b"jianwei &\\r\\n"); rd(3)

print("""
============================================
重要：
1) 模式切换画面在【板子小屏幕】上（大字 VERIFY / GUARD）
2) 终端只会刷文字，例如：
     BOOT down
     mode -> verify
     [VERIFY] AIM 1S
3) 请【不要关闭本窗口】，现在反复短按板上 BOOT
4) 按 Ctrl+C 才会结束监听
============================================
""", flush=True)

try:
    while True:
        rd(1.0)
except KeyboardInterrupt:
    print("\\n结束监听（尽量不复位）", flush=True)
finally:
    try:
        s.dtr = False
        s.rts = False
        time.sleep(0.05)
        s.close()
    except Exception:
        pass
PY
