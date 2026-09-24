#!/usr/bin/env bash
# 硬复位 → 等 nsh → 启动 jianwei → 盯串口看 BOOT
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
PORT=/dev/ttyACM0

echo "=== 1. esptool 硬复位（退出异常/下载模式）==="
sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" \
  --before default_reset --after hard_reset chip_id 2>&1 | tail -15 || true

echo "=== 2. 等串口重新出现 ==="
for i in $(seq 1 40); do
  if [[ -e "$PORT" ]]; then echo "port ok ($i)"; break; fi
  sleep 0.25
done
sleep 4

sudo python3 - <<'PY'
import serial, time, glob, sys

def open_port():
    last = None
    for _ in range(40):
        ports = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
        for p in ports:
            try:
                s = serial.Serial(p, 115200, timeout=0.3)
                # 打开后再拉低，减少复位；关闭前也会拉低
                s.dtr = False
                s.rts = False
                print("opened", p)
                return s
            except Exception as e:
                last = e
        time.sleep(0.4)
    raise SystemExit("no serial: %s" % last)

s = open_port()

def rd(sec):
    b = b""; t = time.time()
    while time.time() - t < sec:
        try:
            c = s.read(4096)
        except Exception as e:
            print("rd-err", e)
            time.sleep(0.2)
            break
        if c:
            b += c
    return b.decode("utf-8", "replace")

def cmd(line, wait=3.0):
    s.reset_input_buffer()
    s.write((line + "\r\n").encode())
    s.flush()
    out = rd(wait)
    print(">>>", line)
    print(out[-1200:] if out.strip() else "(empty)")
    return out

print("===== BOOT LOG =====")
boot = rd(6)
print(boot[-1500:] if boot else "(no boot text)")
print("===== END =====")

# 唤醒 nsh
for _ in range(8):
    s.write(b"\r\n")
    s.flush()
    time.sleep(0.25)
wake = rd(2)
print("wake:", repr(wake[-120:]))

out = cmd("uname -a", 3)
if "NuttX" not in out and "nsh" not in out.lower():
    print("nsh 仍无响应，再硬复位一次建议：重插 USB 后重跑本脚本")
    try:
        s.dtr = False; s.rts = False; s.close()
    except Exception:
        pass
    sys.exit(2)

cmd("ps", 2)
# 清掉旧 jianwei
for _ in range(2):
    cmd("pkill jianwei", 1)
time.sleep(0.4)

out = cmd("jianwei &", 3)
if "jianwei running" not in out and "jianwei [" not in out:
    # 再试一次前台短启动信息
    out = cmd("jianwei &", 3)

print("""
========================================
看到 jianwei running / fb0 后：
  短按 BOOT → 屏切 VERIFY/GUARD，串口 mode ->
本窗口监听 45 秒。Ctrl+C 可停。
========================================
""")
try:
    print(rd(45))
except KeyboardInterrupt:
    print("stop watch")

# 关闭前禁止 RTS/DTR，降低复位概率
try:
    s.dtr = False
    s.rts = False
    time.sleep(0.1)
    s.close()
except Exception:
    pass
print("若之后按键又失灵：再跑一次本脚本即可。")
PY
