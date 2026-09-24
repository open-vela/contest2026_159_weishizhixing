# 见微·随身证

胸前证件卡式反诈助手：**翻开验真，合上守护**。  
硬件：乐鑫 **ESP32-S3-EYE**（官方 openvela BSP 已支持）+ 云端规则 / `ai_agent` Skill。

## 一、作品简介

面向老人隔离场景的可信校验点：短按 BOOT 切换「验真 / 守护」，验真拍照识包装风险，守护自动拾音判电话诈骗话术，三连按向子女看板求助。判断统一在云端 `skills/`，板端只负责感知与展示——体现赛题要求的「主动 + 执行」，而不是纯聊天机器人。

## 二、选题方向

**AI 硬件产品创新**（openvela + ai_agent / Skill）。  
ESP32-S3-EYE 已在 `vendor_espressif` 适配，本作品不走「新硬件适配」赛道。

## 三、目录结构

- `app/jianwei/` — openvela 板端应用（`nsh> jianwei`），manifest 映射到 `packages/demos/contest2026_159_jianwei`
- `skills/` — 反诈规则与 `jianwei-antifraud` Skill（部署到 `/data/agent/skills/`）
- `cloud/` — 自建识别/规则服务与子女看板（赛题模式 A）
- `firmware/jianwei_eye/` — 无 openvela 工具链时的 Arduino 回退（同一协议）
- `docs/` — 协议、架构、编烧与赛道对照
- `logs/` — AI Coding 日志（按手册导出后放入）

## 四、运行方式

### 1. 拉取工程

```bash
repo init -u https://github.com/open-vela/contest2026_159_weishizhixing \
  -b dev-ai-contest-2026 -m contest2026_159_weishizhixing.xml
repo sync -c -j8
```

### 2. 云端

```bash
cd contest2026_159_weishizhixing/cloud
python -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python app.py
```

浏览器打开 `http://127.0.0.1:8787`。`JIANWEI_CLOUD_HOST` / `secrets` 填电脑局域网 IPv4（不要 `127.0.0.1`）。

### 3. 编烧 openvela（ESP32-S3-EYE）

在 openvela 工作区根目录（`repo sync` 后的上一级）：

```bash
# 把 app/jianwei/defconfig.append 并进板级 defconfig，或 menuconfig 勾选「见微随身证」
# JIANWEI_CLOUD_HOST 填电脑局域网 IPv4
./build.sh vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela -j$(nproc)

# 若 make 卡在 esptool 版本检查，可手动：
#   esptool.py -c esp32s3 elf2image --ram-only-header -fs 4MB -fm dio -ff 40m -o nuttx/nuttx.bin nuttx/nuttx

esptool.py --chip esp32s3 --port <PORT> --baud 460800 \
  --before default-reset --after hard-reset \
  write-flash 0x0 nuttx/nuttx.bin
```

本机已验证：`CONFIG_JIANWEI=y` 编出含 `jianwei_main` 的 `nuttx.bin`，`nsh` 可注册 `jianwei` 命令。

板上：

```
nsh> wapi ...   # 联网后
nsh> jianwei
```

短按切验真/守护；1.2s 内三连按求助。Skill 目录拷到 `/data/agent/skills/jianwei-antifraud/`。

细节见 `docs/openvela.md`、`docs/contest.md`。

## 五、AI Coding 使用说明

需求拆解、状态机与协议、openvela 对接与文档由 AI 辅助完成；完整对话日志见 `logs/`（需按官方手册主动导出后放入）。

许可证：Apache-2.0。
