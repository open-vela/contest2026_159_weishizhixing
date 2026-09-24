# SPDX-License-Identifier: Apache-2.0
"""Local OCR + ASR on the demo PC. Board still uploads bytes over HTTP."""
from __future__ import annotations

import os
import tempfile
import threading

os.environ.setdefault("HF_ENDPOINT", "https://hf-mirror.com")
os.environ.setdefault("HF_HUB_DISABLE_XET", "1")

_ocr = None
_ocr_err = ""
_asr = None
_asr_err = ""
_ocr_lock = threading.Lock()
_asr_lock = threading.Lock()


def _decode_image(image_bytes: bytes):
    import cv2
    import numpy as np

    arr = np.frombuffer(image_bytes, dtype=np.uint8)
    img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
    return img


def _load_ocr():
    global _ocr, _ocr_err
    with _ocr_lock:
        if _ocr is not None or _ocr_err:
            return _ocr
        try:
            try:
                from rapidocr_onnxruntime import RapidOCR
            except ImportError:
                from rapidocr import RapidOCR
            _ocr = RapidOCR()
            print("local OCR ready (RapidOCR)", flush=True)
        except Exception as exc:  # noqa: BLE001
            _ocr_err = str(exc)
            print(f"local OCR unavailable: {_ocr_err}", flush=True)
        return _ocr


def _parse_ocr(out) -> str:
    result = out
    if isinstance(out, tuple):
        result = out[0]
    elif isinstance(out, dict):
        result = out.get("result") or out.get("rec_texts") or out.get("txts")
    texts = []
    if not result:
        return ""
    for item in result:
        if isinstance(item, str):
            texts.append(item.strip())
        elif isinstance(item, (list, tuple)) and len(item) >= 2:
            texts.append(str(item[1]).strip())
        elif isinstance(item, dict):
            texts.append(str(item.get("text") or item.get("txt") or "").strip())
    return "\n".join(t for t in texts if t)


def _ocr_once(engine, img) -> str:
    try:
        return _parse_ocr(engine(img, text_score=0.25))
    except TypeError:
        return _parse_ocr(engine(img))


def _prepare_ocr_views(img):
    import cv2

    views = [img]
    h, w = img.shape[:2]
    if max(h, w) < 900:
        views.append(cv2.resize(img, None, fx=2.0, fy=2.0, interpolation=cv2.INTER_CUBIC))
    views.append(cv2.rotate(img, cv2.ROTATE_90_CLOCKWISE))
    views.append(cv2.rotate(img, cv2.ROTATE_90_COUNTERCLOCKWISE))
    return views


def ocr_image_bytes(image_bytes: bytes) -> dict:
    if not image_bytes or len(image_bytes) < 32:
        return {"text": "", "engine": "rapidocr", "error": "图片是空的"}
    engine = _load_ocr()
    if engine is None:
        return {"text": "", "engine": "rapidocr", "error": _ocr_err or "未安装 RapidOCR"}
    try:
        img = _decode_image(image_bytes)
        if img is None:
            return {"text": "", "engine": "rapidocr", "error": "照片解码失败"}
        best = ""
        for view in _prepare_ocr_views(img):
            text = _ocr_once(engine, view)
            if len(text) > len(best):
                best = text
        print(f"ocr local chars={len(best)} text={best[:80]!r}", flush=True)
        if not best:
            return {"text": "", "engine": "rapidocr", "error": "图上没有读到可用文字，请换清晰包装正面再拍。"}
        return {"text": best, "engine": "rapidocr", "error": ""}
    except Exception as exc:  # noqa: BLE001
        return {"text": "", "engine": "rapidocr", "error": f"本地读图失败：{exc}"}


def _whisper_device():
    try:
        import ctranslate2

        if ctranslate2.get_cuda_device_count() > 0:
            return "cuda", "float16"
    except Exception:
        pass
    return "cpu", "int8"


def _load_asr():
    global _asr, _asr_err
    with _asr_lock:
        if _asr is not None or _asr_err:
            return _asr
        try:
            from faster_whisper import WhisperModel

            name = (os.getenv("WHISPER_MODEL") or "small").strip()
            device, ctype = _whisper_device()
            forced = (os.getenv("WHISPER_DEVICE") or "").strip()
            if forced:
                device = forced
                ctype = "float16" if device == "cuda" else "int8"
            attempts = [(device, ctype)]
            if device == "cuda":
                attempts.append(("cpu", "int8"))
            last = None
            for dev, ct in attempts:
                try:
                    print(f"loading Whisper {name} on {dev} {ct} …", flush=True)
                    _asr = WhisperModel(name, device=dev, compute_type=ct)
                    print(f"local ASR ready (faster-whisper {dev})", flush=True)
                    last = None
                    break
                except Exception as exc:  # noqa: BLE001
                    last = exc
                    print(f"Whisper {dev} failed: {exc}", flush=True)
                    _asr = None
            if last is not None and _asr is None:
                raise last
        except Exception as exc:  # noqa: BLE001
            _asr_err = str(exc)
            print(f"local ASR unavailable: {_asr_err}", flush=True)
        return _asr


def asr_wav_bytes(wav_bytes: bytes) -> dict:
    if not wav_bytes or len(wav_bytes) < 200:
        return {"text": "", "error": "录音太短"}
    try:
        import numpy as np

        pcm = np.frombuffer(wav_bytes[44:] if wav_bytes[:4] == b"RIFF" else wav_bytes, dtype=np.int16)
        if pcm.size:
            rms = float(np.sqrt(np.mean(np.square(pcm.astype(np.float32)))))
            peak = int(np.max(np.abs(pcm)))
            print(f"asr wav bytes={len(wav_bytes)} rms={rms:.1f} peak={peak}", flush=True)
            if rms < 60 and peak < 400:
                return {"text": "", "error": "录音几乎没声音，请把免提贴紧板子上的麦克风小孔。"}
    except Exception:
        pass
    engine = _load_asr()
    if engine is None:
        return {"text": "", "error": _asr_err or "未安装 faster-whisper"}
    path = ""
    try:
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            tmp.write(wav_bytes)
            path = tmp.name
        segments, info = engine.transcribe(
            path,
            language="zh",
            vad_filter=False,
            beam_size=1,
            best_of=1,
            temperature=0.0,
            condition_on_previous_text=False,
            initial_prompt="电话 转账 公安 银行 验证码 账户 冻结 保健品 讲座。",
        )
        text = "".join(seg.text for seg in segments).strip()
        lang = getattr(info, "language", "") or ""
        print(f"whisper lang={lang} text={text[:80]!r}", flush=True)
        if not text:
            return {"text": "", "error": "没有听清，请贴紧再大声说完后按 BOOT。"}
        return {"text": text, "error": ""}
    except Exception as exc:  # noqa: BLE001
        return {"text": "", "error": f"本地听写失败：{exc}"}
    finally:
        if path:
            try:
                os.unlink(path)
            except OSError:
                pass


def warmup() -> None:
    def _run():
        _load_ocr()
        _load_asr()

    threading.Thread(target=_run, daemon=True, name="local-ai-warmup").start()
