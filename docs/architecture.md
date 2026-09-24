# 架构

见微是 **一个产品、三处代码、一份协议**。

```
                 ┌──────────── skills/ ────────────┐
                 │ keywords · prices · scripts     │
                 │ jianwei-antifraud/SKILL.md      │
                 └───────────────┬─────────────────┘
                                 │ 只在这里改规则
                                 ▼
┌──────────────┐   docs/protocol.md    ┌──────────────────┐
│ 板端状态机    │ ←──── HTTP JSON ────→ │ cloud/app.py     │
│ app/jianwei  │                       │ 子女看板 + 判断   │
│ 或 Arduino   │                       └────────┬─────────┘
└──────┬───────┘                                │
       │                                        ▼
  /dev/buttons lcd0 video0              浏览器 127.0.0.1:8787
```

- **感知**：BOOT；openvela 上验真走 `/dev/video0`，守护走 `/dev/audio/pcm_in0`。详见 `docs/openvela.md`。
- **决策**：云端规则 + Skill；有 `OPENAI_API_KEY` 时用 `OPENAI_CHAT_MODEL` 做 Skill+LLM（OCR/ASR）；识图用 `OPENAI_VISION_MODEL`。禁止板端再写一套关键词。
- **表达**：LCD 大字等级（HIGH/CHECK/OK）+ 色块；完整中文在子女看板。
- **求助**：长按 BOOT ≥0.8s 发 `help:true`，由云端生成子女事件。

Windows 没有 openvela 交叉编译时，Arduino 固件是 **同一状态机的回退**，不是另一个项目。
