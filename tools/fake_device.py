"""No-board heartbeat, same JSON as firmware."""
from __future__ import annotations

import json
import sys
import urllib.request

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
url = f"http://{HOST}:8787/api/device/heartbeat"
body = json.dumps({"mode": "verify", "help": False}).encode()
req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
print(urllib.request.urlopen(req, timeout=5).read().decode())
