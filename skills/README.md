# Skill 与规则库

本目录是 **整机唯一** 的判断依据，云端 `cloud/app.py` 与将来的 ai_agent 都读这里。

| 文件 | 用途 |
| --- | --- |
| `jianwei-antifraud/SKILL.md` | 给大模型 / ai_agent 的可提交 Skill |
| `keywords.json` | 守护/验真关键词 |
| `prices.json` | 价格带 |
| `license_rules.md` | 备案号格式说明 |
| `scripts.md` | 安抚、脱身、报案 |
| `verify_followups.md` | 验真追问 |

改规则只改这里，不要在 Arduino 或 `jianwei_main.c` 里写死话术。
