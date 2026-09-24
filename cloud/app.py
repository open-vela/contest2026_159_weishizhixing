"""见微·随身证 云端：验真 / 守护 / 设备状态 / 子女看板。"""
from __future__ import annotations

import json
import os
import re
import uuid
from datetime import datetime, timezone
from pathlib import Path
import time

from dotenv import load_dotenv
from flask import Flask, jsonify, render_template, request, send_file
from flask.json.provider import DefaultJSONProvider

MAX_IMAGE_BYTES = 4 * 1024 * 1024

ROOT = Path(__file__).resolve().parent.parent
SKILLS = ROOT / "skills"
DATA = Path(__file__).resolve().parent / "data"
DATA.mkdir(exist_ok=True)
EVENTS_PATH = DATA / "events.json"
VOICE_PATH = DATA / "last_voice.wav"

load_dotenv(ROOT / ".env", override=True)
load_dotenv(Path(__file__).resolve().parent / ".env", override=True)

from local_recog import asr_wav_bytes, ocr_image_bytes, warmup

warmup()

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = MAX_IMAGE_BYTES + 512 * 1024
app.config["TEMPLATES_AUTO_RELOAD"] = True
app.jinja_env.auto_reload = True


class Utf8JSONProvider(DefaultJSONProvider):
    ensure_ascii = False


app.json = Utf8JSONProvider(app)

DEVICE = {
    "mode": "guard",
    "online": False,
    "last_seen": None,
    "last_seen_ts": 0.0,
    "last_result": None,
    "help": False,
    "last_photo_ts": 0.0,
    "last_photo_name": "",
    "last_photo_mime": "image/jpeg",
}


def device_view() -> dict:
    online = (time.time() - float(DEVICE.get("last_seen_ts") or 0)) < 12
    DEVICE["online"] = online
    return {
        "mode": DEVICE["mode"],
        "online": online,
        "last_seen": DEVICE["last_seen"],
        "help": DEVICE["help"],
        "last_result": DEVICE["last_result"],
        "photo_ts": DEVICE.get("last_photo_ts") or 0,
    }


def now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


def voice_line(event: dict) -> str:
    label = str(event.get("level_label") or "").strip()
    advice = str(event.get("advice") or "").replace("\\n", " ").replace("\n", " ").strip()
    if len(advice) > 48:
        advice = advice[:48]
    text = "。".join(part for part in (label, advice) if part)
    return text[:72]


def synthesize_voice(event: dict) -> None:
    """Generate a short Chinese WAV for the board speaker. Failures are ignored."""
    text = voice_line(event)
    VOICE_PATH.unlink(missing_ok=True)
    if not text:
        return
    api_key = os.getenv("OPENAI_API_KEY")
    if not api_key:
        print("tts skip: no key", flush=True)
        return
    import urllib.error
    import urllib.request

    url = os.getenv("OPENAI_BASE_URL", "https://api.openai.com/v1").rstrip("/") + "/audio/speech"
    model = (os.getenv("OPENAI_TTS_MODEL") or "FunAudioLLM/CosyVoice2-0.5B").strip()
    voice = (os.getenv("OPENAI_TTS_VOICE") or f"{model}:anna").strip()
    body = json.dumps(
        {
            "model": model,
            "input": text,
            "voice": voice,
            "response_format": "wav",
            "sample_rate": 16000,
            "speed": 0.9,
        }
    ).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=body,
        headers={"Authorization": f"Bearer {api_key}", "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=20) as resp:
        wav = resp.read()
    if len(wav) < 44 or wav[:4] != b"RIFF":
        print(f"tts bad audio bytes={len(wav)}", flush=True)
        return
    VOICE_PATH.write_bytes(wav)
    print(f"tts wav={len(wav)} text={text}", flush=True)


def load_json(path: Path, default):
    if not path.exists():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def load_events() -> list:
    return load_json(EVENTS_PATH, [])


def save_events(events: list) -> None:
    EVENTS_PATH.write_text(json.dumps(events, ensure_ascii=False, indent=2), encoding="utf-8")


def append_event(event: dict) -> dict:
    events = load_events()
    events.insert(0, event)
    save_events(events[:200])
    DEVICE["last_result"] = event
    if event.get("kind") in ("verify", "guard"):
        label = str(event.get("level_label") or "")
        if label in ("没听清",) or (event.get("kind") == "verify" and not (event.get("ocr_text") or event.get("text"))):
            print("tts skip: no useful result", flush=True)
        else:
            try:
                synthesize_voice(event)
            except Exception as exc:  # noqa: BLE001
                print(f"tts skip: {exc}", flush=True)
    return event


def save_last_photo(raw: bytes, mime: str) -> None:
    mime = (mime or "image/jpeg").split(";")[0].strip().lower()
    if raw[:2] == b"BM":
        mime = "image/bmp"
        name = "last_pack.bmp"
    elif "png" in mime:
        name = "last_pack.png"
    else:
        mime = "image/jpeg"
        name = "last_pack.jpg"
    for old in DATA.glob("last_pack.*"):
        old.unlink(missing_ok=True)
    (DATA / name).write_bytes(raw)
    DEVICE["last_photo_name"] = name
    DEVICE["last_photo_mime"] = mime
    DEVICE["last_photo_ts"] = time.time()


def read_incoming_audio() -> tuple[bytes, str]:
    upload = request.files.get("audio")
    if upload:
        return upload.read(MAX_IMAGE_BYTES + 1), upload.filename or "guard.wav"
    raw = request.get_data(cache=False) or b""
    if len(raw) > MAX_IMAGE_BYTES + 1:
        raw = raw[: MAX_IMAGE_BYTES + 1]
    return raw, "guard.wav"


def read_incoming_image() -> tuple[bytes, str]:
    upload = request.files.get("image")
    if upload and (upload.filename or upload.content_type):
        raw = upload.read(MAX_IMAGE_BYTES + 1)
        mime = (upload.mimetype or "image/jpeg").split(";")[0].strip().lower()
        return raw, mime
    raw = request.get_data(cache=False) or b""
    if len(raw) > MAX_IMAGE_BYTES + 1:
        raw = raw[: MAX_IMAGE_BYTES + 1]
    mime = (request.content_type or "image/jpeg").split(";")[0].strip().lower()
    return raw, mime


KEYWORDS = load_json(SKILLS / "keywords.json", {"keywords": []})
PRICES = load_json(SKILLS / "prices.json", {"items": []})
SCRIPTS = (SKILLS / "scripts.md").read_text(encoding="utf-8") if (SKILLS / "scripts.md").exists() else ""
FOLLOWUPS = (SKILLS / "verify_followups.md").read_text(encoding="utf-8") if (SKILLS / "verify_followups.md").exists() else ""
SKILL_MD = ""
_skill_path = SKILLS / "jianwei-antifraud" / "SKILL.md"
if _skill_path.exists():
    SKILL_MD = _skill_path.read_text(encoding="utf-8")
LICENSE_RULES = (SKILLS / "license_rules.md").read_text(encoding="utf-8") if (SKILLS / "license_rules.md").exists() else ""


def chat_model_name() -> str:
    """文本理解用聊天模型；视觉模型名可单独配，避免 VL 模型被误用于纯文本时被整段跳过。"""
    return (
        os.getenv("OPENAI_CHAT_MODEL")
        or os.getenv("JIANWEI_LLM_MODEL")
        or os.getenv("OPENAI_MODEL")
        or "gpt-4o-mini"
    ).strip()


def vision_model_name() -> str:
    return (
        os.getenv("OPENAI_VISION_MODEL")
        or os.getenv("OPENAI_MODEL")
        or chat_model_name()
    ).strip()

LICENSE_RE = re.compile(
    r"(国食健注[GJ]\d{8}|食健备[A-Za-z\u4e00-\u9fff]{1,6}\d{4,12}|国药准字[A-Z]\d{8})"
)
PRICE_RE = re.compile(r"(\d+(?:\.\d+)?)\s*元")


def score_to_level(score: int) -> str:
    if score >= 70:
        return "high"
    if score >= 35:
        return "mid"
    return "low"


def level_label(level: str) -> str:
    return {"high": "高风险", "mid": "需核实", "low": "未见明显风险"}[level]


def extract_script(section: str) -> str:
    parts = SCRIPTS.split("## ")
    for p in parts:
        if p.startswith(section):
            return p.split("\n", 1)[-1].strip()
    return ""


def keyword_hits(text: str) -> tuple[int, list[str]]:
    score = 0
    hits = []
    for item in KEYWORDS.get("keywords", []):
        w = item["word"]
        if w and w in text:
            score += int(item.get("score", 10))
            hits.append(w)
    urgent = any(x in text for x in ("立刻", "马上", "现在就", "紧急", "来不及"))
    money = any(x in text for x in ("转账", "汇款", "安全账户", "打款"))
    isolate = any(x in text for x in ("不要告诉", "不许说", "不让子女", "保密", "不准问"))
    if urgent and money and isolate:
        score += 40
        hits.append("组合:紧急+要钱+不让核实")
    return min(score, 100), hits


def check_license(text: str) -> dict:
    found = LICENSE_RE.findall(text)
    if not found:
        if any(x in text for x in ("内部批文", "内部号", "内部编号", "特批")):
            return {"ok": False, "codes": [], "note": "出现非公开「内部批文」说法，编号不可信"}
        return {"ok": None, "codes": [], "note": "未提供备案号，无法做格式校验"}
    bad = []
    for code in found:
        if "内部" in code:
            bad.append(code)
        if code.startswith("国食健注") and not re.fullmatch(r"国食健注[GJ]\d{8}", code):
            bad.append(code)
    if bad:
        return {"ok": False, "codes": found, "note": "编号格式明显不符合常见官方规律"}
    return {"ok": True, "codes": found, "note": "格式看起来像常见备案号，仍需自行在官网核对"}


def check_price(text: str) -> dict:
    amount = None
    m = PRICE_RE.search(text)
    if m:
        amount = float(m.group(1))
    matched = None
    for item in PRICES.get("items", []):
        names = [item["name"], *item.get("aliases", [])]
        if any(n and n in text for n in names):
            matched = item
            break
    if not matched or amount is None:
        return {"flag": False, "note": "没有同时识别到品类和价格，跳过价格异常"}
    if amount < matched["low"] or amount > matched["high"]:
        return {
            "flag": True,
            "note": f"{matched['name']}报价 {amount} 元，常见区间约 {matched['low']}-{matched['high']} {matched['unit']}",
        }
    return {"flag": False, "note": f"{matched['name']}报价在常见区间内"}


def channel_risk(text: str) -> tuple[int, str]:
    for ch, pts, name in (
        ("讲座", 25, "讲座推销"),
        ("免费旅游", 25, "免费旅游/旅游团"),
        ("旅游团", 20, "旅游团"),
        ("微信群", 15, "微信群"),
        ("专家", 10, "专家推荐话术"),
    ):
        if ch in text:
            return pts, name
    return 0, ""


def hype_hits(text: str) -> list[str]:
    phrases = [
        "包治", "根治", "药到病除", "祖传", "国家级", "纳米", "基因修复",
        "当天见效", "无效退款", "医院不告诉你", "内部指标", "限时抢",
    ]
    return [p for p in phrases if p in text]


def verify_text(text: str) -> dict:
    text = (text or "").strip()
    score = 0
    reasons = []
    license_info = check_license(text)
    if license_info["ok"] is False:
        score += 35
        reasons.append(license_info["note"])
    elif license_info["ok"] is None:
        score += 10
        reasons.append(license_info["note"])
    else:
        reasons.append(license_info["note"])

    price_info = check_price(text)
    if price_info["flag"]:
        score += 25
        reasons.append(price_info["note"])
    else:
        reasons.append(price_info["note"])

    hype = hype_hits(text)
    if hype:
        score += min(10 * len(hype), 30)
        reasons.append("宣传含夸大话术：" + "、".join(hype))

    ch_pts, ch_name = channel_risk(text)
    if ch_pts:
        score += ch_pts
        reasons.append(f"购买渠道加分：{ch_name}")

    kw_score, kw = keyword_hits(text)
    if kw:
        score += min(kw_score // 2, 20)
        reasons.append("文案关键词：" + "、".join(kw[:8]))

    score = min(score, 100)
    level = score_to_level(score)
    follow = []
    for line in FOLLOWUPS.splitlines():
        if line.strip().startswith(("1.", "2.", "3.", "4.")):
            follow.append(line.strip())
    advice = {
        "high": "先不要付款。把包装上的备案号拿到「国家市场监督管理总局」相关查询页核对，并告诉子女。",
        "mid": "可以记下编号和价格，回家后和家人一起查。讲座或群里的限时优惠先别信。",
        "low": "目前文字里没有特别夸张的承诺。仍建议在正规药店或超市购买，并保留小票。",
    }[level]
    return {
        "id": uuid.uuid4().hex[:10],
        "kind": "verify",
        "time": now_iso(),
        "text": text,
        "score": score,
        "level": level,
        "level_label": level_label(level),
        "reasons": reasons,
        "followups": follow[:3],
        "advice": advice,
        "license": license_info,
    }


def guard_text(text: str) -> dict:
    text = (text or "").strip()
    score, hits = keyword_hits(text)
    if "验证码" in text and any(x in text for x in ("告诉我", "念给我", "发给我")):
        score = min(score + 25, 100)
        hits.append("索要验证码")
    level = score_to_level(score)
    steps = []
    if level == "high":
        steps = [
            extract_script("安抚") or "先别转账，你没有做错。",
            extract_script("核实") or "用通讯录原号码回拨。",
            extract_script("暂缓转账") or "不要转到所谓安全账户。",
            extract_script("报案") or "可拨打 96110。",
        ]
    elif level == "mid":
        steps = [
            extract_script("安抚") or "先停一停。",
            extract_script("核实") or "挂断后回拨官方号码。",
        ]
    else:
        steps = ["这段话里还没有典型诈骗组合。若对方催转账，随时再让我听一次。"]
    return {
        "id": uuid.uuid4().hex[:10],
        "kind": "guard",
        "time": now_iso(),
        "text": text,
        "score": score,
        "level": level,
        "level_label": level_label(level),
        "hits": hits,
        "reasons": [f"命中：{h}" for h in hits] or ["未命中高风险关键词"],
        "advice": steps[0],
        "steps": [s for s in steps if s],
        "escape": extract_script("脱身"),
    }


def has_cjk(text: str) -> bool:
    return bool(re.search(r"[\u4e00-\u9fff]", text or ""))


def maybe_llm(kind: str, text: str, base: dict) -> dict:
    """有密钥时用大模型（对齐 openvela ai_agent Skill）理解 OCR/ASR 文本并给出结论。

    规则引擎始终先跑；大模型在有 KEY 时做主理解与润色。不再因模型名含 VL 而整段跳过。
    """
    api_key = os.getenv("OPENAI_API_KEY")
    if not api_key:
        base["engine"] = base.get("engine") or "rules"
        base["ai"] = "off-no-key"
        return base
    text = (text or "").strip()
    if len(text) < 2:
        base["engine"] = base.get("engine") or "rules"
        base["ai"] = "off-short-text"
        return base
    model = chat_model_name()
    # 纯 VL 型号若不能做 chat，可另设 OPENAI_CHAT_MODEL；否则仍尝试
    try:
        import urllib.request

        url = os.getenv("OPENAI_BASE_URL", "https://api.openai.com/v1").rstrip("/") + "/chat/completions"
        skill_snip = (SKILL_MD or "")[:3500]
        rules_snip = json.dumps(
            {
                "keywords_sample": (KEYWORDS.get("keywords") or [])[:40],
                "prices_sample": (PRICES.get("items") or [])[:15],
                "scripts_head": (SCRIPTS or "")[:800],
                "license_rules_head": (LICENSE_RULES or "")[:600],
            },
            ensure_ascii=False,
        )
        prompt = (
            "你是 openvela 端侧反诈 Skill「jianwei-antifraud」的执行器。"
            "根据包装 OCR 或电话 ASR 文本，输出严格 JSON："
            '{"level":"high|mid|low","score":0-100,"reasons":["..."],"advice":"..."}。\n'
            "要求：语气温和、不指责老人；禁止说「已通过国家认证」；"
            "高风险守护优先：安抚→回拨原号→暂缓转账→96110。\n"
            f"## Skill\n{skill_snip}\n"
            f"## 规则摘要\n{rules_snip}\n"
            f"## 模式\n{kind}\n"
            f"## 用户文本\n{text}\n"
            f"## 规则初判\n{json.dumps(base, ensure_ascii=False)}\n"
            "只输出一个 JSON 对象。"
        )
        body = json.dumps(
            {
                "model": model,
                "messages": [
                    {
                        "role": "system",
                        "content": "你是见微随身证反诈助手，严格按 Skill 输出 JSON。",
                    },
                    {"role": "user", "content": prompt},
                ],
                "temperature": 0.2,
            }
        ).encode("utf-8")
        req = urllib.request.Request(
            url,
            data=body,
            headers={"Authorization": f"Bearer {api_key}", "Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=45) as resp:
            payload = json.loads(resp.read().decode("utf-8"))
        content = payload["choices"][0]["message"]["content"]
        m = re.search(r"\{.*\}", content, re.S)
        if not m:
            raise ValueError("no json")
        extra = json.loads(m.group(0))
        if extra.get("level") in ("high", "mid", "low"):
            base["level"] = extra["level"]
            base["level_label"] = level_label(base["level"])
        if isinstance(extra.get("score"), int):
            base["score"] = extra["score"]
        if extra.get("reasons"):
            base["reasons"] = extra["reasons"]
        if extra.get("advice"):
            base["advice"] = extra["advice"]
        base["engine"] = "skill+llm"
        base["llm_model"] = model
        base["ai"] = f"on:{model}"
        base["skill"] = "jianwei-antifraud"
    except Exception as exc:  # noqa: BLE001 — 演示时必须可降级
        base["engine"] = base.get("engine") or "rules"
        base["llm_error"] = str(exc)
        base["ai"] = f"fallback-rules:{exc}"
    return base


def bmp_to_png(data: bytes) -> bytes | None:
    """openvela 板端上传 QVGA RGB565 转成的 24 位 BMP，Vision API 要 PNG/JPEG。"""
    import struct
    import zlib

    if len(data) < 54 or data[:2] != b"BM":
        return None
    off = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if width <= 0 or abs(height) <= 0 or bpp != 24:
        return None
    top_down = height < 0
    height = abs(height)
    row_bytes = (width * 3 + 3) & ~3
    need = off + row_bytes * height
    if len(data) < need:
        return None
    rows = []
    for y in range(height):
        src_y = y if top_down else (height - 1 - y)
        start = off + src_y * row_bytes
        bgr = data[start : start + width * 3]
        rgb = bytearray(width * 3)
        for i in range(width):
            rgb[i * 3 + 0] = bgr[i * 3 + 2]
            rgb[i * 3 + 1] = bgr[i * 3 + 1]
            rgb[i * 3 + 2] = bgr[i * 3 + 0]
        rows.append(b"\x00" + bytes(rgb))
    raw = b"".join(rows)

    def chunk(tag: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )


def extract_packaging_text(image_bytes: bytes, mime: str) -> dict:
    """读包装图上的文字。优先本机 RapidOCR，失败再走云端视觉。"""
    png = bmp_to_png(image_bytes)
    if png:
        image_bytes = png
        mime = "image/png"
    local = ocr_image_bytes(image_bytes)
    if local.get("text"):
        print(f"ocr local chars={len(local['text'])}", flush=True)
        return local
    api_key = os.getenv("OPENAI_API_KEY")
    if not api_key:
        return {
            "text": "",
            "engine": local.get("engine") or "rapidocr",
            "error": local.get("error") or "本地读图失败，且未配置云端密钥",
        }
    import base64
    import urllib.error
    import urllib.request

    if mime not in ("image/jpeg", "image/jpg", "image/png", "image/webp"):
        mime = "image/jpeg"
    b64 = base64.b64encode(image_bytes).decode("ascii")
    url = os.getenv("OPENAI_BASE_URL", "https://api.openai.com/v1").rstrip("/") + "/chat/completions"
    prompt = (
        "这是保健品/药品包装或传单照片。只做两件事："
        "1) 尽量完整抄出图上可见的中文、数字、备案号、价格；"
        "2) 列出夸张承诺、内部批文、限时、专家、讲座等推销话术。"
        "不要判断是不是真药，不要说官方认证。"
        '只输出 JSON：{"text":"抄录全文","claims":["话术1"]}'
    )
    configured = vision_model_name()
    models = []
    for name in (
        configured,
        "Qwen/Qwen3-VL-8B-Instruct",
        "Pro/Qwen/Qwen3-VL-8B-Instruct",
        "Qwen/Qwen2.5-VL-7B-Instruct",
        "Pro/Qwen/Qwen2.5-VL-7B-Instruct",
    ):
        if name and name not in models:
            models.append(name)
    last_err = "读图失败"
    for model in models:
        body = json.dumps(
            {
                "model": model,
                "messages": [
                    {
                        "role": "user",
                        "content": [
                            {"type": "text", "text": prompt},
                            {"type": "image_url", "image_url": {"url": f"data:{mime};base64,{b64}"}},
                        ],
                    }
                ],
                "temperature": 0.1,
                "max_tokens": 1200,
            }
        ).encode("utf-8")
        req = urllib.request.Request(
            url,
            data=body,
            headers={"Authorization": f"Bearer {api_key}", "Content-Type": "application/json"},
        )
        try:
            with urllib.request.urlopen(req, timeout=70) as resp:
                payload = json.loads(resp.read().decode("utf-8"))
            content = payload["choices"][0]["message"]["content"]
            m = re.search(r"\{.*\}", content, re.S)
            if not m:
                return {"text": content.strip(), "engine": "vision", "error": ""}
            extra = json.loads(m.group(0))
            text = str(extra.get("text") or "").strip()
            claims = extra.get("claims") or []
            if isinstance(claims, list) and claims:
                text = (text + "\n" + " ".join(str(c) for c in claims)).strip()
            if not text:
                return {"text": "", "engine": "vision", "error": "图上没有读到可用文字，请换清晰包装正面再拍。"}
            return {"text": text, "engine": "vision", "error": ""}
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")[:400]
            last_err = f"读图接口失败 HTTP {exc.code} model={model}: {detail}"
            if "20012" in detail or "does not exist" in detail.lower():
                continue
            return {"text": "", "engine": "vision", "error": last_err}
        except Exception as exc:  # noqa: BLE001
            return {"text": "", "engine": "vision", "error": f"读图失败：{exc}"}
    return {"text": "", "engine": "vision", "error": last_err}


def pcm16_to_wav(pcm: bytes, rate: int = 16000) -> bytes:
    import struct

    n = len(pcm)
    return struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + n,
        b"WAVE",
        b"fmt ",
        16,
        1,
        1,
        rate,
        rate * 2,
        2,
        16,
        b"data",
        n,
    ) + pcm


def transcribe_audio(file_bytes: bytes, filename: str) -> dict:
    raw = file_bytes if file_bytes[:4] == b"RIFF" else pcm16_to_wav(file_bytes)
    try:
        (DATA / "last_guard.wav").write_bytes(raw)
    except OSError:
        pass
    local = asr_wav_bytes(raw)
    if local.get("text"):
        print(f"asr local: {local['text'][:80]}", flush=True)
        return local
    print(f"asr local empty: {local.get('error')}", flush=True)
    return {"text": "", "error": local.get("error") or "没有听清"}


@app.get("/health")
def health():
    return jsonify({"ok": True, "service": "jianwei"})


@app.get("/")
def home():
    return render_template("index.html", device=device_view(), events=load_events()[:20])


@app.post("/api/verify")
def api_verify():
    text = request.json.get("text", "") if request.is_json else request.form.get("text", "")
    result = maybe_llm("verify", text, verify_text(text))
    append_event(result)
    DEVICE["mode"] = "verify"
    return jsonify(result)


@app.post("/api/verify-image")
def api_verify_image():
    """包装照片 → 抽文字 → 同一套 verify 规则。支持网页 multipart 或板子直接 POST JPEG。"""
    raw, mime = read_incoming_image()
    print(f"verify-image mime={mime} bytes={len(raw)}", flush=True)
    if len(raw) > MAX_IMAGE_BYTES:
        result = {
            "id": uuid.uuid4().hex[:10],
            "kind": "verify",
            "time": now_iso(),
            "text": "",
            "score": 40,
            "level": "mid",
            "level_label": "需核实",
            "reasons": ["照片过大"],
            "advice": "靠近包装再拍一张小一些的图。",
            "engine": "upload",
            "source": "packaging_photo",
            "ocr_text": "",
        }
        append_event(result)
        DEVICE["mode"] = "verify"
        return jsonify(result)
    if len(raw) >= 32:
        save_last_photo(raw, mime)
    if len(raw) < 32:
        result = {
            "id": uuid.uuid4().hex[:10],
            "kind": "verify",
            "time": now_iso(),
            "text": "",
            "score": 40,
            "level": "mid",
            "level_label": "需核实",
            "reasons": ["没有收到照片"],
            "advice": "请再对准包装短按一次。网页也可上传照片。",
            "engine": "upload",
            "source": "packaging_photo",
            "ocr_text": "",
        }
        append_event(result)
        DEVICE["mode"] = "verify"
        return jsonify(result)
    if raw[:2] == b"BM":
        mime = "image/bmp"
    extracted = extract_packaging_text(raw, mime)
    if extracted["error"] and not extracted["text"]:
        result = {
            "id": uuid.uuid4().hex[:10],
            "kind": "verify",
            "time": now_iso(),
            "text": "",
            "score": 40,
            "level": "mid",
            "level_label": "需核实",
            "reasons": [extracted["error"]],
            "advice": "照片已传到网页。字没读清，请再靠近拍一次，或在文字框里改完再点开始验真。",
            "engine": extracted.get("engine") or "vision",
            "source": "packaging_photo",
            "ocr_text": "",
            "ocr_warning": extracted["error"],
        }
        append_event(result)
        DEVICE["mode"] = "verify"
        return jsonify(result)
    result = maybe_llm("verify", extracted["text"], verify_text(extracted["text"]))
    result["source"] = "packaging_photo"
    result["ocr_text"] = extracted["text"]
    result["ocr_engine"] = extracted["engine"]
    if extracted.get("warning"):
        result["ocr_warning"] = extracted["warning"]
    if extracted["error"]:
        result["ocr_warning"] = extracted["error"]
    append_event(result)
    DEVICE["mode"] = "verify"
    return jsonify(result)


@app.post("/api/guard")
def api_guard():
    text = request.json.get("text", "") if request.is_json else request.form.get("text", "")
    result = maybe_llm("guard", text, guard_text(text))
    append_event(result)
    DEVICE["mode"] = "guard"
    return jsonify(result)


@app.post("/api/guard-audio")
def api_guard_audio():
    raw, fname = read_incoming_audio()
    print(f"guard-audio bytes={len(raw)}", flush=True)
    if len(raw) > MAX_IMAGE_BYTES:
        raw = b""
    if len(raw) < 200:
        heard = {"text": "", "error": "板子录音没解析到，请说完再按 BOOT"}
    else:
        heard = transcribe_audio(raw, fname)
    if heard.get("error") and not heard.get("text"):
        result = {
            "id": uuid.uuid4().hex[:10],
            "kind": "guard",
            "time": now_iso(),
            "text": "",
            "score": 40,
            "level": "mid",
            "level_label": "没听清",
            "hits": [],
            "reasons": [heard["error"]],
            "advice": "没听清，请把免提贴紧，说完再按一次 BOOT。网页也可点「听 5 秒」。",
            "steps": ["贴紧麦克风，说完再按 BOOT。"],
            "engine": "asr",
            "source": "guard_audio",
            "asr_text": "",
        }
        append_event(result)
        DEVICE["mode"] = "guard"
        return jsonify(result)
    text = heard.get("text") or ""
    if not has_cjk(text) or len(text.strip()) < 4:
        result = {
            "id": uuid.uuid4().hex[:10],
            "kind": "guard",
            "time": now_iso(),
            "text": text,
            "score": 40,
            "level": "mid",
            "level_label": "没听清",
            "hits": [],
            "reasons": ["录音里几乎没有中文，多半是没贴紧麦克风"],
            "advice": "请把手机免提贴紧板子上的小孔，大声说完再按 BOOT。",
            "steps": ["贴紧麦克风，大声说完再按一次。"],
            "engine": "asr",
            "source": "guard_audio",
            "asr_text": text,
        }
        append_event(result)
        DEVICE["mode"] = "guard"
        return jsonify(result)
    result = maybe_llm("guard", text, guard_text(text))
    result["source"] = "guard_audio"
    result["asr_text"] = text
    if not result.get("engine") or result.get("engine") == "rules":
        result["engine"] = "asr+rules"
    elif result.get("engine") == "skill+llm":
        result["engine"] = "asr+skill+llm"
    if heard.get("error"):
        result["asr_warning"] = heard["error"]
    append_event(result)
    DEVICE["mode"] = "guard"
    return jsonify(result)


@app.post("/api/device/heartbeat")
def heartbeat():
    body = request.get_json(silent=True) or {}
    DEVICE["last_seen"] = now_iso()
    DEVICE["last_seen_ts"] = time.time()
    DEVICE["online"] = True
    if body.get("mode") in ("verify", "guard"):
        DEVICE["mode"] = body["mode"]
    help_flag = bool(body.get("help")) or int(body.get("help_clicks") or 0) >= 3
    if help_flag:
        DEVICE["help"] = True
        append_event(
            {
                "id": uuid.uuid4().hex[:10],
                "kind": "help",
                "time": now_iso(),
                "text": "设备长按求助",
                "score": 90,
                "level": "high",
                "level_label": "求助",
                "reasons": ["隐蔽求助信号"],
                "advice": extract_script("求助确认") or "已通知家人。",
                "engine": "device",
            }
        )
    last = DEVICE["last_result"] or {}
    return jsonify(
        {
            "ok": True,
            "mode": DEVICE["mode"],
            "help": DEVICE["help"],
            "last": last,
            "level_label": last.get("level_label", ""),
            "advice": last.get("advice", ""),
        }
    )


@app.post("/api/device/mode")
def set_mode():
    mode = (request.get_json(silent=True) or {}).get("mode")
    if mode in ("verify", "guard"):
        DEVICE["mode"] = mode
    return jsonify({"ok": True, "mode": DEVICE["mode"]})


@app.post("/api/help/clear")
def clear_help():
    DEVICE["help"] = False
    return jsonify({"ok": True})


@app.get("/api/device/photo")
def get_device_photo():
    name = DEVICE.get("last_photo_name") or ""
    path = DATA / name if name else None
    if not path or not path.exists():
        return ("", 404)
    return send_file(path, mimetype=DEVICE.get("last_photo_mime") or "image/jpeg")


@app.get("/api/device/voice")
def get_device_voice():
    path = DATA / "last_voice.wav"
    if not path.exists() or path.stat().st_size < 44:
        return ("", 404)
    return send_file(path, mimetype="audio/wav")


@app.get("/api/device")
def get_device():
    return jsonify(device_view())


@app.get("/api/ai-status")
def ai_status():
    """赛题对齐说明：风险判定走云端 Skill+LLM（openvela ai_agent 叙事），板端只采集。"""
    key = bool(os.getenv("OPENAI_API_KEY"))
    return jsonify(
        {
            "skill": "jianwei-antifraud",
            "skill_loaded": bool(SKILL_MD),
            "openai_key": key,
            "chat_model": chat_model_name() if key else "",
            "vision_model": os.getenv("OPENAI_VISION_MODEL")
            or os.getenv("OPENAI_MODEL")
            or "",
            "mode": "device-http + cloud skill+llm (openvela Mode A)",
            "on_device_ai_agent": False,
            "note": "本仓库无板端 ai_agent 包；判定在云端执行 Skill+聊天模型，与赛题「自定义 Skill」一致。",
        }
    )


@app.get("/api/events")
def get_events():
    return jsonify(load_events()[:50])


if __name__ == "__main__":
    import socket

    host = os.getenv("HOST", "0.0.0.0")
    port = int(os.getenv("PORT", "8787"))

    def lan_ip() -> str:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.connect(("223.5.5.5", 80))
            return sock.getsockname()[0]
        except OSError:
            return "127.0.0.1"
        finally:
            sock.close()

    ip = lan_ip()
    print(f"本机演示  http://127.0.0.1:{port}")
    print(f"板子填写  CLOUD_HOST={ip}  PORT={port}")
    print(f"手机打开  http://{ip}:{port}")
    app.run(host=host, port=port, debug=False, threaded=True)
