#!/usr/bin/env bash
# 串口诊断：等 nsh、查 WiFi、手动连热点、起 jianwei、看心跳
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
PORT=/dev/ttyACM0
VM_IP=$(hostname -I | awk '{print $1}')
SSID="${1:-mfl}"
PASS="${2:-123456mn}"

echo "VM_IP=$VM_IP  SSID=$SSID"
ls -l "$PORT"

# 硬复位出下载模式
sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" \
  --before default_reset --after hard_reset chip_id >/dev/null 2>&1 || true
sleep 4

sudo python3 - <<PY
import serial, time, urllib.request, re, sys
port = "$PORT"
vm = "$VM_IP"
ssid = "$SSID"
passwd = "$PASS"

s = serial.Serial(port, 115200, timeout=0.2)
# USB-JTAG reset dance
for _ in range(2):
    try:
        s.dtr = False
        s.rts = True
        time.sleep(0.1)
        s.rts = False
        time.sleep(0.8)
    except Exception:
        pass

def rd(sec):
    b = b""
    t = time.time()
    while time.time() - t < sec:
        c = s.read(4096)
        if c:
            b += c
    return b.decode("utf-8", "replace")

def cmd(line, wait=4):
    s.reset_input_buffer()
    s.write((line + "\r\n").encode())
    s.flush()
    out = rd(wait)
    print("========== >>>", line)
    print(out if out.strip() else "(empty)")
    print("==========")
    return out

print("===== BOOT =====")
boot = rd(8)
print(boot[-2000:] if boot else "(no boot text)")
print("===== END BOOT =====")

# wake nsh
for _ in range(5):
    s.write(b"\r\n")
    s.flush()
    time.sleep(0.4)
wake = rd(2)
print("wake:", repr(wake[-200:]))

out = cmd("uname -a", 3)
if "NuttX" not in out and "nsh" not in out.lower():
    print("WARN: nsh 可能未就绪，继续试...")

cmd("help", 3)
cmd("ifconfig", 3)
cmd("ifconfig wlan0", 3)

# 手动连 WiFi（若自动没拿到 IP）
cmd(f"wapi psk wlan0 {passwd} 3", 3)
cmd(f"wapi essid wlan0 {ssid}", 4)
cmd("wapi mode wlan0 managed", 2)
# DHCP: renew / dhcp
for dhcp_cmd in ("renew wlan0", "dhcp wlan0", "ifup wlan0"):
    cmd(dhcp_cmd, 3)

board_ip = None
for i in range(12):
    out = cmd("ifconfig wlan0", 2)
    m = re.search(r"inet addr:([0-9.]+)", out)
    if m:
        board_ip = m.group(1)
        print("BOARD_IP", board_ip)
        break
    time.sleep(2)

if board_ip:
    cmd(f"ping -c 3 {vm}", 10)
else:
    print("仍无 IP：请确认手机热点名/密码是", ssid, passwd)

# 后台跑 jianwei
cmd("jianwei &", 2)
# 有的构建不支持 &，再试前台短等
time.sleep(1)
print("--- listen for beats ---")
beats = rd(16)
print(beats[-1500:] if beats else "(no beats)")

s.close()
time.sleep(1)
try:
    body = urllib.request.urlopen("http://127.0.0.1:8787/api/device", timeout=3).read().decode()
    print("DEVICE", body)
except Exception as e:
    print("api", e)
    # try start cloud hint
    sys.exit(2)

print("看板 http://%s:8787" % vm)
PY
