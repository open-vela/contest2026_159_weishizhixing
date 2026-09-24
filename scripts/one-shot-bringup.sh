#!/usr/bin/env bash
# 一次性：自启 + 稳定按键/屏 + 重编烧录 + 起云端 + 自动重连监听
# 用法：bash scripts/one-shot-bringup.sh
set -euo pipefail
export PATH="$HOME/.local/bin:/home/ubuntu/vela-opensource/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
ROOT=/home/ubuntu/jianwei-id
NUTTX=/home/ubuntu/vela-opensource/nuttx
SRC=$ROOT/app/jianwei
DST=/home/ubuntu/contest2026_159_weishizhixing_work/app/jianwei
PORT=/dev/ttyACM0

echo "========== 1) sync sources =========="
cp -f "$SRC/jianwei_main.c" "$SRC/jianwei_ui.c" "$SRC/defconfig.append" "$DST/"
bash "$ROOT/scripts/apply-autostart.sh"

echo "========== 2) ensure button + input config =========="
python3 - <<'PY'
from pathlib import Path
import re
p = Path("/home/ubuntu/vela-opensource/nuttx/.config")
t = p.read_text()
for b in [
    "CONFIG_INPUT=y",
    "CONFIG_ARCH_BUTTONS=y",
    "CONFIG_ARCH_IRQBUTTONS=y",
    "CONFIG_INPUT_BUTTONS=y",
    "CONFIG_INPUT_BUTTONS_LOWER=y",
    "CONFIG_JIANWEI=y",
]:
    key = b.split("=")[0]
    t = re.sub(rf'^# {re.escape(key)} is not set\s*\n', '', t, flags=re.M)
    t = re.sub(rf'^{re.escape(key)}=.*\n', '', t, flags=re.M)
    t += b + "\n"
p.write_text(t)
print("config forced")
PY
cd "$NUTTX"
make olddefconfig >/tmp/one-shot-olddef.log 2>&1 || true
grep -E '^CONFIG_INPUT=|^CONFIG_INPUT_BUTTONS=|^CONFIG_ARCH_BUTTONS=|^CONFIG_JIANWEI=' .config

echo "========== 3) rebuild (keep libapps; refresh board+jianwei) =========="
rm -f "$NUTTX"/boards/xtensa/esp32s3/esp32s3-eye/src/esp32s3_bringup*.o \
      "$NUTTX"/boards/xtensa/esp32s3/esp32s3-eye/src/esp32s3_buttons*.o \
      "$NUTTX"/drivers/input/button_*.o \
      "$NUTTX"/staging/libboards.a "$NUTTX"/staging/libdrivers.a \
      /home/ubuntu/vela-opensource/apps/packages/demos/contest2026_159_jianwei/jianwei_*.o \
      /home/ubuntu/vela-opensource/apps/packages/demos/contest2026_159_jianwei/.built
touch "$NUTTX/boards/xtensa/esp32s3/esp32s3-eye/src/esp32s3_bringup.c"
set +e
make EXTRAFLAGS=-Wno-cpp -j"$(nproc)" >/tmp/one-shot-build.log 2>&1
rc=$?
set -e
echo "make_rc=$rc"
tail -15 /tmp/one-shot-build.log | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' | tr -d '\r'
[[ -f "$NUTTX/nuttx" ]] || { grep -E 'error:|undefined' /tmp/one-shot-build.log | tail -40; exit 1; }
nm "$NUTTX/nuttx" | grep -E 'jianwei_autostart|jianwei_main|btn_lower_initialize' | head -10
strings "$NUTTX/nuttx" | grep -E 'VERIFY|fb0|autostart' | head -8

esptool.py -c esp32s3 elf2image --ram-only-header --dont-append-digest \
  -fs 4MB -fm dio -ff 40m -o "$NUTTX/nuttx.bin" "$NUTTX/nuttx"

echo "========== 4) start cloud =========="
pkill -f 'python3 -u app.py' 2>/dev/null || true
sleep 1
cd "$ROOT/cloud"
# shellcheck disable=SC1091
source "$ROOT/.venv/bin/activate" 2>/dev/null || true
nohup python3 -u app.py >/tmp/jianwei-cloud.log 2>&1 &
for i in $(seq 1 30); do
  curl -sf http://127.0.0.1:8787/api/ai-status >/tmp/ai-status.json && break
  sleep 0.3
done
echo "AI status:"; cat /tmp/ai-status.json 2>/dev/null || echo "(cloud not up yet)"
VM_IP=$(hostname -I | awk '{print $1}')
echo "看板 http://$VM_IP:8787"

echo "========== 5) flash =========="
sudo fuser -k /dev/ttyACM0 2>/dev/null || true
sleep 1
flash_ok=0
for a in 1 2 3 4 5; do
  echo "flash attempt $a"
  P=$(ls /dev/ttyACM* 2>/dev/null | head -1 || true)
  [[ -n "${P:-}" ]] || { sleep 2; continue; }
  if sudo -E env PATH="$PATH" esptool.py -c esp32s3 -p "$P" -b 115200 \
    --before usb_reset --after hard_reset --no-stub \
    write_flash --flash_size keep -fm dio -ff 40m \
    0x0 "$NUTTX/nuttx.bin"; then
    flash_ok=1; break
  fi
  sleep 3
done
[[ "$flash_ok" == 1 ]] || { echo FLASH_FAIL; exit 1; }

echo "========== 6) wait autostart (~12s) then watch =========="
sleep 12
sudo python3 - <<PY
import serial, time, glob, sys, json, urllib.request

def open_port():
    while True:
        for p in sorted(glob.glob("/dev/ttyACM*")+glob.glob("/dev/ttyUSB*")):
            try:
                s=serial.Serial(p,115200,timeout=0.25)
                s.dtr=False; s.rts=False
                print("[opened]",p,flush=True); return s
            except Exception as e:
                print("[open]",p,e,flush=True)
        time.sleep(0.5)

def pump(s, sec):
    t=time.time(); buf=""
    while time.time()-t<sec:
        try:
            c=s.read(4096)
        except Exception as e:
            print("\\n[掉线]",e,flush=True); return False, buf
        if c:
            txty=c.decode("utf-8","replace")
            sys.stdout.write(txty); sys.stdout.flush(); buf+=txty
    return True, buf

try:
    st=urllib.request.urlopen("http://127.0.0.1:8787/api/ai-status",timeout=3).read().decode()
    print("AI:", st, flush=True)
except Exception as e:
    print("AI status fail", e, flush=True)

s=open_port()
ok,buf=pump(s,4)
if "jianwei running" not in buf:
    try:
        s.write(b"\\r\\njianwei &\\r\\n"); s.flush()
    except Exception:
        pass
    ok,buf2=pump(s,3); buf+=buf2

print("""
================================================
已开机自启 jianwei。请短按 BOOT：
  小屏：VERIFY READY ↔ GUARD READY
  终端：mode -> verify / guard
关本窗口后板子上 jianwei 仍在（自启）。
Ctrl+C 结束监听。掉线会自动重连。
================================================
""", flush=True)
try:
    while True:
        ok, _ = pump(s, 1.0)
        if not ok:
            try: s.close()
            except Exception: pass
            time.sleep(1); s=open_port()
except KeyboardInterrupt:
    print("\\nbye", flush=True)
finally:
    try:
        s.dtr=False; s.rts=False; s.close()
    except Exception:
        pass
PY
