# 设备 ↔ 云端协议

Arduino 回退固件、openvela `jianwei` 应用、网页看板共用这一套字段。改一处，三端一起改。

基址：`http://<电脑局域网IPv4>:8787`（不要用 `127.0.0.1` 给板子）。

## `POST /api/verify`

```json
{ "text": "包装或传单上的字" }
```

返回：`kind, level, level_label, score, reasons, advice, followups`。

## `POST /api/verify-image`

`multipart/form-data`，字段名 **`image`**（jpeg/png/webp，或 openvela 板端 24 位 BMP，≤4MB）。网页或板子相机均可。

云端先抽包装上的文字和推销话术，再走与 `/api/verify` 相同的 `skills/` 规则。

返回同上，另含 `source: "packaging_photo"`、`ocr_text`。失败时 `400` + `{ "error": "..." }`。

不做真假药鉴定；无 `OPENAI_API_KEY` 时读图不可用，请改打字验真。

## `POST /api/guard`

```json
{ "text": "老人转述的电话/讲座内容" }
```

返回：`kind, level, level_label, hits, reasons, advice, steps, escape`。

## `POST /api/guard-audio`

`multipart/form-data`，字段名 **`audio`**（wav，16kHz 单声道优先）。网页麦克风或板子 PDM/I2S 麦均可。

云端听写后再走 `/api/guard` 同一套规则。返回另含 `source: "guard_audio"`、`asr_text`。

## `POST /api/device/heartbeat`

```json
{ "mode": "verify" | "guard", "help": false }
```

`help: true` 表示长按 BOOT 求助。

返回：

```json
{ "ok": true, "mode": "guard", "help": false, "last": { "level_label": "...", "advice": "..." } }
```

板端只把 `last.level_label` / `last.advice` 显示在 LCD 或串口。

## `POST /api/device/mode`  `POST /api/help/clear`  `GET /api/device`  `GET /api/events`

网页看板使用。判断引擎只读 `skills/`，不在固件里复制规则。
