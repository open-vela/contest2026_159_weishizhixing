#!/usr/bin/env bash
# 修复被掏空的 libapps.a，完整重编 apps + 固件，再烧录测 BOOT
set -euo pipefail
export PATH="$HOME/.local/bin:/home/ubuntu/vela-opensource/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
PORT=/dev/ttyACM0
SRC=/home/ubuntu/jianwei-id/app/jianwei
DST=/home/ubuntu/contest2026_159_weishizhixing_work/app/jianwei
NUTTX=/home/ubuntu/vela-opensource/nuttx

echo "=== sync button-fix sources ==="
cp -f "$SRC/jianwei_main.c" "$SRC/jianwei_ui.c" "$DST/"
grep -n "BOOT short" "$DST/jianwei_main.c"

echo "=== apps_clean（恢复完整 libapps，上一版被误删）==="
cd "$NUTTX"
make apps_clean 2>&1 | tail -20

echo "=== full make（MKIMAGE esptool 版本报错可忽略）==="
set +e
make EXTRAFLAGS=-Wno-cpp -j"$(nproc)" >/tmp/rebuild-full-btn.log 2>&1
make_rc=$?
set -e
echo "make_rc=$make_rc"
tail -25 /tmp/rebuild-full-btn.log | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' | tr -d '\r'

# libapps 应含 nsh + jianwei
echo "=== check libapps.a ==="
ar t /home/ubuntu/vela-opensource/apps/libapps.a | wc -l
nm /home/ubuntu/vela-opensource/apps/libapps.a | grep -E ' T nsh_main| T jianwei_main' || {
  echo "libapps still broken"; exit 1;
}

[[ -f "$NUTTX/nuttx" ]] || {
  echo "nuttx ELF missing — 若只有 MKIMAGE 失败却无 ELF，见日志末尾"
  grep -E 'undefined reference|error:|Error' /tmp/rebuild-full-btn.log | tail -20
  exit 1
}

echo "=== verify ELF strings ==="
strings "$NUTTX/nuttx" | grep -F "BOOT short" | head -2
nm "$NUTTX/nuttx" | grep -E ' nsh_main| jianwei_main' | head -5

echo "=== elf2image ==="
esptool.py -c esp32s3 elf2image --ram-only-header --dont-append-digest \
  -fs 4MB -fm dio -ff 40m -o "$NUTTX/nuttx.bin" "$NUTTX/nuttx"
ls -lh "$NUTTX/nuttx.bin"

echo "=== flash ==="
flash_ok=0
for attempt in 1 2 3; do
  echo "--- attempt $attempt ---"
  if sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" -b 115200 \
    --before usb_reset --after hard_reset --no-stub \
    write_flash --flash_size keep -fm dio -ff 40m \
    0x0 "$NUTTX/nuttx.bin"; then
    flash_ok=1; break
  fi
  sleep 2
done
[[ "$flash_ok" == 1 ]] || { echo FLASH_FAIL; exit 1; }

sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$PORT" \
  --before default_reset --after hard_reset chip_id >/dev/null 2>&1 || true
sleep 6

# 云端
if ! curl -sf -o /dev/null http://127.0.0.1:8787/; then
  cd /home/ubuntu/jianwei-id/cloud
  # shellcheck disable=SC1091
  source /home/ubuntu/jianwei-id/.venv/bin/activate 2>/dev/null || true
  nohup python3 -u app.py >/tmp/jianwei-cloud.log 2>&1 &
  sleep 2
fi

sudo python3 - <<'PY'
import serial, time
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
    print(out[-900:] if out.strip() else "(empty)")
    return out

print("boot:", rd(6)[-500:])
s.write(b"\r\n"); rd(0.5)
cmd("uname -a", 2)
cmd("jianwei &", 2)
print("==== 15s 内短按 BOOT（不是 RST）2～3 次 ====")
buf = rd(15)
print(buf)
ok = ("mode ->" in buf) or ("BOOT down" in buf) or ("BOOT short" in buf)
print("结果:", "按键 OK" if ok else "仍无按键边沿 —— 把输出发我")
s.close()
PY
