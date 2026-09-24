# 赛道对照与提交

## 为什么不是「新硬件适配」

[赛道指引](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/hardware_porting/hardware_porting_track_guide.md) 写明：**已适配的开发板不计入该方向**。

ESP32-S3-EYE 已在 `open-vela/vendor_espressif` 的 `dev-ai-contest-2026` 提供 camera / LCD / mic / Wi-Fi / BOOT。本作品是在这块 **已适配板** 上做产品，对应 **AI 硬件产品创新**。

硬件手册仍要读，用来正确调用节点，而不是再写一份 BSP：

- 板级：https://github.com/open-vela/vendor_espressif/blob/dev-ai-contest-2026/boards/esp32s3/esp32s3-eye/README_zh-cn.md
- 模组：https://github.com/espressif/esp-who/blob/master/docs/zh_CN/get-started/ESP32-S3-EYE_Getting_Started_Guide.md
- 产品赛道导航：https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_guide_index.md
- 代码提交：https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md

## GitHub 专属仓

1. 接受 `open-vela/contest2026_<编号>_<队名>` 协作邀请（报名那个 GitHub 账号）。
2. 签署 [CLA](https://openvela.com/#/community/cla)。
3. 把本仓库文件拷进专属仓（或把专属仓加为 remote 后推送），**fork → PR → 自己合入**。
4. `logs/` 里放导出的 AI Coding 日志。
5. 另交：作品介绍（`docs/见微随身证_openvela作品提交.docx`）、≤5 分钟演示视频、仓库地址。

截止：**9 月 20 日**。协议 **Apache-2.0**。

## 评分时我们对齐的点

| 指标 | 本仓库怎么交 |
| --- | --- |
| 技术难度 | 用官方 BSP 的 video/lcd/audio/wifi 节点；应用状态机 + 云端 Skill，而不是重复移植 |
| 产品创新 | 证件卡双模式、隔离场景下的可信校验点 |
| 完整度 | 云端 + `nsh> jianwei` 真机（拍照/听音/求助）+ Arduino 回退 + 文档 |
| AI 开发 | `skills/jianwei-antifraud` + 对话日志 |
| 展示 | `docs/demo.md` |
