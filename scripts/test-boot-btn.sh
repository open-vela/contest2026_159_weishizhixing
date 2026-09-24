#!/usr/bin/env bash
# 重启 jianwei，并盯串口 15 秒看按键是否生效
set -euo pipefail
PORT=/dev/ttyACM0

sudo python3 - <<'PY'
import serial, time, re

s = serial.Serial("/dev/ttyACM0", 115200, timeout=0.25)

def rd(sec):
    b = b""; t = time.time()
    while time.time() - t < sec:
        c = s.read(4096)
        if c: b += c
    return b.decode("utf-8", "replace")

def cmd(line, wait=2.5):
    s.write((line + "\r\n").encode()); s.flush()
    out = rd(wait)
    print(">>>", line)
    print(out[-1000:] if out.strip() else "(empty)")
    return out

s.write(b"\r\n"); rd(0.3)
print("=== 结束旧的 jianwei ===")
# 找到 jianwei 的 pid 再杀
out = cmd("ps", 2)
for m in re.finditer(r"^\s*(\d+)\s+\d+\s+\d+\s+(\S+)", out, re.M):
    pass
# NuttX ps 格式多变，粗暴杀几次
for _ in range(3):
    s.write(b"pkill jianwei\r\n"); rd(0.4)
    # 若有 pid 行含 jianwei
for m in re.finditer(r"(\d+)\s+.*jianwei", out):
    cmd("kill %s" % m.group(1), 1)

time.sleep(0.5)
print("\n=== 重新启动 jianwei ===")
cmd("jianwei &", 2)

print("""
========================================
请看清板子靠 USB 口那两颗键：
  - 一颗是 RST（复位）→ 不要按
  - 一颗是 BOOT → 按这颗

现在 15 秒内：短按 BOOT 2～3 次（按一下马上松开）
串口应出现：mode -> verify  或  mode -> guard
========================================
""")
buf = rd(15)
print(buf)
ok = ("mode ->" in buf) or ("HELP" in buf) or ("VERIFY" in buf) or ("AIM" in buf)
print("\n结果:", "有反应 OK" if ok else "仍无 mode 切换 —— 把这段输出发我")
s.close()
PY
