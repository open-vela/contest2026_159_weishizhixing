#!/usr/bin/env bash
set -euo pipefail
BR=/home/ubuntu/vela-opensource/nuttx/boards/xtensa/esp32s3/esp32s3-eye/src/esp32s3_bringup.c
if grep -q 'jianwei_autostart' "$BR"; then
  echo "autostart already patched"
  exit 0
fi
python3 - <<'PY'
from pathlib import Path
p = Path("/home/ubuntu/vela-opensource/nuttx/boards/xtensa/esp32s3/esp32s3-eye/src/esp32s3_bringup.c")
t = p.read_text()
hook = '''
#ifdef CONFIG_JIANWEI
extern int jianwei_main(int argc, char *argv[]);

static int jianwei_autostart(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  sleep(8);
  {
    char *args[2] =
      {
        "jianwei", NULL
      };

    return jianwei_main(1, args);
  }
}
#endif
'''
inc_mark = '#include "esp32s3-eye.h"\n'
if inc_mark not in t:
    raise SystemExit("include mark missing")
t = t.replace(inc_mark, inc_mark + "\n" + hook + "\n", 1)
needle = "  UNUSED(ret);\n  return OK;\n}"
repl = '''#ifdef CONFIG_JIANWEI
  /* Auto-start 见微：关串口也不丢应用 */
  {
    int jpid = task_create("jianwei", 100, 32768, jianwei_autostart, NULL);
    if (jpid < 0)
      {
        syslog(LOG_ERR, "jianwei autostart failed: %d\\n", jpid);
      }
    else
      {
        syslog(LOG_INFO, "jianwei autostart pid=%d\\n", jpid);
      }
  }
#endif

  UNUSED(ret);
  return OK;
}'''
if needle not in t:
    raise SystemExit("return OK marker missing")
t = t.replace(needle, repl, 1)
if "#include <nuttx/sched.h>" not in t:
    t = t.replace("#include <syslog.h>", "#include <syslog.h>\n#include <nuttx/sched.h>")
p.write_text(t)
print("patched", p)
PY
grep -n 'jianwei_autostart\|task_create' "$BR" | head
