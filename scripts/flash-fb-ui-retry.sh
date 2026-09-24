#!/usr/bin/env bash
# 固件已编好时：只重烧 + 启动 jianwei 测屏（带重试）
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
PORT=/dev/ttyACM0
BIN=/home/ubuntu/vela-opensource/nuttx/nuttx.bin
NUTTX=/home/ubuntu/vela-opensource/nuttx

cd "$NUTTX"
if [[ ! -f nuttx.bin ]] || ! strings nuttx | grep -q 'open /dev/fb0'; then
  echo "ELF 缺 fb0 字符串，先跑 rebuild-flash-fb-ui.sh"
  exit 1
fi

# 若 bin 比 elf 旧，重出镜像
if [[ nuttx -nt nuttx.bin ]]; then
  esptool.py -c esp32s3 elf2image --ram-only-header --dont-append-digest \
    -fs 4MB -fm dio -ff 40m -o nuttx.bin nuttx
fi
ls -lh nuttx.bin

flash_ok=0
for attempt in 1 2 3 4 5; do
  echo "=== flash attempt $attempt ==="
  # 等端口稳定
  for i in $(seq 1 15); do
    [[ -e "$PORT" ]] && break
    sleep 0.5
  done
  sleep 1
  if sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
    --before usb_reset --after hard_reset --no-stub \
    write_flash --flash_size keep -fm dio -ff 40m \
    0x0 "$BIN"; then
    flash_ok=1
    break
  fi
  echo "flash fail, wait and retry (可拔插 USB / 按住 BOOT 再试)"
  sleep 3
done
[[ "$flash_ok" == 1 ]] || { echo FLASH_FAIL; exit 1; }

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
print("==== 12s 短按 BOOT：屏应切换 VERIFY/GUARD；串口应有 fb0 240x240 ====")
buf = rd(12)
print(buf)
ok = ("fb0" in (out + buf)) and (("mode ->" in buf) or ("BOOT" in buf) or ("VERIFY" in buf) or ("GUARD" in buf))
print("结果:", "屏驱动已起来" if "fb0" in (out+buf) else "fb0 未成功 —— 发输出")
s.close()
PY
