# openvela 对接（赛题正式板端）

正式板端是 `app/jianwei/`。赛题模式 **A**：设备 HTTP + 云端识别/规则。  
Arduino `firmware/jianwei_eye/` 只证明同一协议能在这块 ESP32-S3-EYE 上跑通，**不是**另一套产品。

赛题不允许只交纯云端。提交 GitHub 专属仓时必须包含 `app/jianwei` + `skills/jianwei-antifraud` + `cloud/`。在 Ubuntu/WSL 编烧后，串口执行 `jianwei`。

作品介绍（官方模板已填）：`docs/见微随身证_openvela作品提交.docx`。

## 0. 和赛题条目怎么对上

| 要求 | 落点 |
| --- | --- |
| 设备上跑 openvela 应用 | `nsh> jianwei`，源码 `app/jianwei/` |
| 官方已适配板，不重复 BSP | ESP32-S3-EYE，`vendor_espressif` |
| 自定义 Skill ≥ 1 | `skills/jianwei-antifraud/` → 设备 `/data/agent/skills/jianwei-antifraud/` |
| 主动 + 执行 | 进入守护自动拾音并 POST；长按 BOOT ≥0.8s 求助心跳 |
| 图形 / 多媒体 / AI | `/dev/lcd0`；`/dev/video0` + `/dev/audio/pcm_in0`；云端 OCR/ASR + `skills/` |
| 唤醒词（若开语音渠道） | 你好，openvela |
| 分支 | `dev-ai-contest-2026` |

## 1. 专属仓怎么放本仓库

组委会仓名：`contest2026_<编号>_<队名>`。`repo init` / `repo sync` 后把本仓库对应目录拷进去（manifest 会把 `app/jianwei` 映到 `packages/demos/...`）：

| 本仓库 | 专属仓 |
| --- | --- |
| `app/jianwei/` | `app/jianwei/` |
| `skills/` | 随应用；Skill 再拷到板上 `/data/agent/skills/` |
| `cloud/` | 自建服务端（模式 A 允许） |
| `firmware/jianwei_eye/` | 开发回退，介绍文档须写明 |
| `logs/` | 按手册导出的 AI Coding 日志 |
| `docs/见微随身证_openvela作品提交.docx` | 作品介绍 |

压缩包命名示例：`<团队名称>-见微随身证-contest2026_<编号>_<队名>.zip`。

## 2. menuconfig

```bash
cd openvela/nuttx
make menuconfig
```

`Application Configuration` → 勾选 **见微随身证**。  
`JIANWEI_CLOUD_HOST` = 电脑局域网 IPv4（不要 `127.0.0.1`），端口默认 8787。

或把 `app/jianwei/defconfig.append` 并进板级 `configs/openvela/defconfig`。

## 3. 编译烧录

```bash
cd openvela
./build.sh $(pwd)/vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela -j$(nproc)
esptool --chip esp32s3 --port <PORT> --baud 460800 \
  --before default-reset --after hard-reset \
  write-flash 0x0 nuttx/nuttx.bin
```

## 4. 板上联网后启动

电脑先只开一份 `cloud/app.py`。然后：

```
nsh> wapi mode wlan0 2
nsh> wapi psk wlan0 <密码> 3 wpa
nsh> wapi essid wlan0 <SSID> 1
nsh> renew wlan0
nsh> ping -c 2 <电脑IP>
nsh> jianwei
```

按键：短按松开立即切验真/守护（验真拍 `/dev/video0` 转 BMP 上传，守护录 `/dev/audio/pcm_in0`）；长按 ≥0.8s 求助。OCR/ASR 文本经云端规则 + Skill 驱动 LLM（`OPENAI_CHAT_MODEL`）理解，识图用 `OPENAI_VISION_MODEL`。

## 5. Skill

把 `skills/jianwei-antifraud/` 放到 `/data/agent/skills/jianwei-antifraud/`。  
判断仍由电脑 `cloud/app.py` 读同一份 `skills/`，避免两套词表。

## 6. 当前完成度（写进介绍、不要夸大）

- 已完成：协议、状态机源码、云端、Skill 文本、Arduino 真机三条闭环、作品介绍模板。  
- 已完成：本机 openvela（ESP32-S3-EYE）编出含 `jianwei` 的 `nuttx.bin`（`Register: jianwei` / 符号 `jianwei_main` 等）。  
- 待完成：真机烧录演示视频（`nsh> jianwei`）、专属仓 PR 合入、`logs/` 按手册导出。
