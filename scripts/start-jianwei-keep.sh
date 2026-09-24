#!/usr/bin/env bash
# 不复位板子：重新拉起 jianwei，然后你可以一直按 BOOT 看屏
set -euo pipefail
PORT=/dev/ttyACM0

sudo python3 - <<'PY'
import serial, time, glob, sys

def open_port():
    for _ in range(20):
        for p in sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")):
            try:
                s = serial.Serial()
                s.port = p
                s.baudrate = 115200
                s.timeout = 0.25
                # 关键：禁止 DTR/RTS，避免关串口时把 ESP32-S3 复位掉
                s.dtr = False
                s.rts = False
                s.open()
                s.dtr = False
                s.rts = False
                print("opened", p)
                return s
            except Exception as e:
                print("open fail", p, e)
        time.sleep(0.4)
    raise SystemExit("no serial")

s = open_port()

def rd(sec):
    b = b""; t = time.time()
    while time.time() - t < sec:
        try:
            c = s.read(4096)
        except Exception as e:
            print("rd", e); break
        if c: b += c
    return b.decode("utf-8", "replace")

def cmd(line, wait=2):
    s.write((line + "\r\n").encode()); s.flush()
    out = rd(wait)
    print(">>>", line)
    print(out[-800:] if out.strip() else "(empty)")
    return out

# 清一下，不要复位
s.write(b"\r\n"); rd(0.5)
# 若已有 jianwei，先杀再启
out = cmd("ps", 2)
for line in out.splitlines():
    if "jianwei" in line and "jianwei_main" not in line:
        parts = line.split()
        if parts and parts[0].isdigit():
            cmd("kill " + parts[0], 1)

cmd("pkill jianwei", 1)
time.sleep(0.3)
cmd("jianwei &", 2)

print("""
========================================
jianwei 已在后台跑。
现在可以反复短按 BOOT 看屏幕 VERIFY/GUARD。
本窗口会继续打印串口 60 秒；也可 Ctrl+C 结束（不会复位板子）。
结束后 jianwei 仍会继续跑。
========================================
""")
try:
    print(rd(60))
except KeyboardInterrupt:
    print("\nstopped watch")
# 显式保持 DTR/RTS 低再关，降低复位概率
try:
    s.dtr = False
    s.rts = False
    time.sleep(0.05)
    s.close()
except Exception:
    pass
print("串口已关，板子上的 jianwei 应仍在。继续按 BOOT 即可。")
PY
