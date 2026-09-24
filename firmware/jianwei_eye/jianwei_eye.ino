/*
 * 见微·随身证 — ESP32-S3-EYE
 * 短按松开 BOOT：立即切换验真/守护（不再等 1.2s 连按窗口）
 * 长按 BOOT ≥0.8s：求助子女
 * 进入守护：对着麦说话，说完再短按 BOOT 结束并上传
 * 进入验真约 1s 后拍照
 * 只需 ESP32 板卡包。
 */
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"
#include "jianwei_hw.h"

const int PIN_BOOT = 0;
const int PIN_LED = 3;

enum Mode { MODE_GUARD, MODE_VERIFY };
Mode mode = MODE_GUARD;

unsigned long lastBeat = 0;
unsigned long lastBtnChange = 0;
bool lastBtn = true;
bool bootHeld = false;
unsigned long bootDownAt = 0;
bool helpArmed = false;
unsigned long shotAt = 0;
bool pendingShot = false;
unsigned long listenAt = 0;
bool pendingListen = false;
String lastAdvice = "waiting cloud";
String lastLevel = "-";
String lastLevelCode = "";
bool photoOnLcd = false;

void setLed(bool on) {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

String modeName() { return mode == MODE_VERIFY ? "verify" : "guard"; }

String jsonString(const String &json, const char *key) {
  String pat = String("\"") + key + "\":\"";
  int i = json.indexOf(pat);
  if (i < 0) {
    pat = String("\"") + key + "\": \"";
    i = json.indexOf(pat);
  }
  if (i < 0) {
    return "";
  }
  i += pat.length();
  int j = json.indexOf("\"", i);
  if (j < 0) {
    return "";
  }
  return json.substring(i, j);
}

void applyResult(const String &payload) {
  String lv = jsonString(payload, "level_label");
  String code = jsonString(payload, "level");
  String ad = jsonString(payload, "advice");
  if (code.length()) {
    lastLevelCode = code;
  }
  if (lv.length()) {
    lastLevel = lv;
  }
  if (ad.length()) {
    lastAdvice = ad;
    lastAdvice.replace("\\n", " ");
    if (lastAdvice.length() > 80) {
      lastAdvice = lastAdvice.substring(0, 80);
    }
  }
}

void refreshLcd(const char *line2) {
  photoOnLcd = false;
  uint16_t bg = COL_BG;
  const char *risk = line2 ? line2 : "OK";
  if (lastLevelCode == "high") {
    bg = COL_HIGH;
    risk = "HIGH";
  } else if (lastLevelCode == "mid") {
    bg = COL_MID;
    risk = "CHECK";
  } else if (lastLevelCode == "low") {
    bg = COL_LOW;
    risk = "OK";
  }
  const char *l1 = (mode == MODE_VERIFY) ? "VERIFY" : "GUARD";
  lcdStatus(l1, risk, bg);
}

void showResultBanner() {
  uint16_t bg = COL_BG;
  const char *risk = "OK";
  if (lastLevelCode == "high") {
    bg = COL_HIGH;
    risk = "HIGH";
  } else if (lastLevelCode == "mid") {
    bg = COL_MID;
    risk = "CHECK";
  } else if (lastLevelCode == "low") {
    bg = COL_LOW;
    risk = "OK";
  }
  if (photoOnLcd) {
    lcdBanner(risk, bg);
  } else {
    refreshLcd(NULL);
  }
}

void playCloudVoice() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  HTTPClient http;
  String url = String("http://") + CLOUD_HOST + ":" + CLOUD_PORT + "/api/device/voice";
  http.setTimeout(25000);
  http.setConnectTimeout(8000);
  http.begin(url);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("voice fail %d\n", code);
    http.end();
    return;
  }
  int hinted = http.getSize();
  size_t cap = (hinted > 44 && hinted <= 600000) ? (size_t)hinted : 200000;
  uint8_t *buf = (uint8_t *)ps_malloc(cap);
  if (!buf) {
    http.end();
    Serial.println("voice oom");
    return;
  }
  WiFiClient *stream = http.getStreamPtr();
  size_t got = 0;
  unsigned long t0 = millis();
  while (got < cap && millis() - t0 < 20000) {
    int avail = stream->available();
    if (avail <= 0) {
      if (!stream->connected() && avail <= 0) {
        break;
      }
      delay(2);
      continue;
    }
    size_t room = cap - got;
    int chunk = avail < (int)room ? avail : (int)room;
    int r = stream->readBytes(buf + got, chunk);
    if (r <= 0) {
      break;
    }
    got += (size_t)r;
    if (hinted > 0 && got >= (size_t)hinted) {
      break;
    }
  }
  http.end();
  bool ok = spkPlayWav(buf, got);
  Serial.printf("voice %u play=%d gpio%d\n", (unsigned)got, ok, SPK_PIN);
  free(buf);
}

void sendHeartbeat(bool help) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  HTTPClient http;
  String url = String("http://") + CLOUD_HOST + ":" + CLOUD_PORT + "/api/device/heartbeat";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"mode\":\"") + modeName() + "\",\"help\":" + (help ? "true" : "false") + "}";
  int code = http.POST(body);
  if (code == 200) {
    String payload = http.getString();
    applyResult(payload);
    Serial.printf("beat %d mode=%s level=%s\n", code, modeName().c_str(), lastLevel.c_str());
    if (!photoOnLcd) {
      refreshLcd(help ? "HELP" : "CLOUD");
    }
  } else {
    Serial.printf("beat fail %d\n", code);
    if (!photoOnLcd) {
      lcdStatus(mode == MODE_VERIFY ? "VERIFY" : "GUARD", "NO NET", COL_MID);
    }
  }
  http.end();
}

bool sendPackPhoto() {
  if (!camReady || WiFi.status() != WL_CONNECTED) {
    Serial.println("photo skip (cam/wifi)");
    lcdStatus("VERIFY", "NO CAM", COL_MID);
    return false;
  }
  lcdStatus("VERIFY", "PHOTO", COL_WAIT);
  for (int i = 0; i < 2; i++) {
    camera_fb_t *dump = esp_camera_fb_get();
    if (dump) {
      esp_camera_fb_return(dump);
    }
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb || fb->len < 100) {
    if (fb) {
      esp_camera_fb_return(fb);
    }
    Serial.println("photo empty");
    lcdStatus("VERIFY", "CAM FAIL", COL_HIGH);
    return false;
  }
  if (lcdShowJpeg(fb->buf, fb->len)) {
    photoOnLcd = true;
    lcdBanner("WAIT", COL_WAIT);
    Serial.println("photo on lcd");
  }

  HTTPClient http;
  String url = String("http://") + CLOUD_HOST + ":" + CLOUD_PORT + "/api/verify-image";
  http.setTimeout(90000);
  http.setConnectTimeout(15000);
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  Serial.printf("photo jpeg %u\n", (unsigned)fb->len);
  int code = http.POST(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  if (code == 200) {
    String payload = http.getString();
    applyResult(payload);
    Serial.printf("photo 200 level=%s\n", lastLevel.c_str());
    showResultBanner();
    playCloudVoice();
  } else {
    String resp = http.getString();
    Serial.printf("photo fail %d %s\n", code, resp.substring(0, 120).c_str());
    photoOnLcd = false;
    lcdStatus("VERIFY", "UP FAIL", COL_HIGH);
  }
  http.end();
  return code == 200;
}

bool sendGuardAudio() {
  if (!micReady || WiFi.status() != WL_CONNECTED) {
    Serial.println("listen skip");
    lcdStatus("GUARD", "NO MIC", COL_MID);
    return false;
  }
  lcdStatus("GUARD", "TALK", COL_WAIT);
  Serial.println("listen talk, press BOOT when done");
  size_t wavLen = 0;
  uint8_t *wav = micCaptureWavUntilBoot(8000, &wavLen);
  lastBtn = digitalRead(PIN_BOOT);
  bootHeld = false;
  helpArmed = false;
  if (!wav || wavLen < 1000) {
    Serial.println("listen empty");
    if (wav) {
      free(wav);
    }
    lcdStatus("GUARD", "MIC FAIL", COL_MID);
    return false;
  }
  lcdStatus("GUARD", "WAIT", COL_WAIT);
  HTTPClient http;
  String url = String("http://") + CLOUD_HOST + ":" + CLOUD_PORT + "/api/guard-audio";
  http.setTimeout(90000);
  http.setConnectTimeout(15000);
  http.begin(url);
  http.addHeader("Content-Type", "audio/wav");
  Serial.printf("listen wav %u\n", (unsigned)wavLen);
  int code = http.POST(wav, wavLen);
  free(wav);
  if (code == 200) {
    applyResult(http.getString());
    Serial.printf("listen 200 level=%s\n", lastLevel.c_str());
    photoOnLcd = false;
    refreshLcd(NULL);
    playCloudVoice();
  } else {
    String resp = http.getString();
    Serial.printf("listen fail %d %s\n", code, resp.substring(0, 120).c_str());
    lcdStatus("GUARD", "ASR FAIL", COL_MID);
  }
  http.end();
  lastBtn = digitalRead(PIN_BOOT);
  bootHeld = false;
  helpArmed = false;
  return code == 200;
}

void enterMode(Mode next) {
  mode = next;
  Serial.printf("mode -> %s\n", modeName().c_str());
  setLed(mode == MODE_VERIFY);
  photoOnLcd = false;
  if (mode == MODE_VERIFY) {
    pendingListen = false;
    pendingShot = true;
    shotAt = millis() + 1000;
    lcdStatus("VERIFY", "AIM 1S", COL_WAIT);
  } else {
    pendingShot = false;
    pendingListen = true;
    listenAt = millis() + 200;
    lcdStatus("GUARD", "TALK", COL_WAIT);
  }
}

void doHelp() {
  Serial.println("HELP");
  pendingShot = false;
  pendingListen = false;
  setLed(true);
  lastLevelCode = "high";
  photoOnLcd = false;
  lcdStatus("HELP", "FAMILY", COL_HELP);
  sendHeartbeat(true);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  setLed(false);
  lcdBegin();
  if (camBegin()) {
    Serial.println("cam ok");
  } else {
    Serial.println("cam fail");
  }
  if (micBegin()) {
    Serial.println("mic ok");
    lcdStatus("JIANWEI", "CAM MIC", COL_LOW);
  } else {
    Serial.println("mic fail");
    lcdStatus("JIANWEI", camReady ? "NO MIC" : "NO CAM", COL_MID);
  }
  delay(400);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : " offline, will retry");
  lcdStatus("GUARD", WiFi.status() == WL_CONNECTED ? "WIFI OK" : "NO WIFI", COL_BG);
}

void loop() {
  bool btn = digitalRead(PIN_BOOT);
  if (btn != lastBtn && millis() - lastBtnChange > 40) {
    lastBtnChange = millis();
    lastBtn = btn;
    if (btn == LOW) {
      bootHeld = true;
      bootDownAt = millis();
      helpArmed = false;
    } else if (bootHeld) {
      unsigned long held = millis() - bootDownAt;
      bootHeld = false;
      if (helpArmed || held >= 800) {
        helpArmed = false;
        doHelp();
      } else if (held >= 40) {
        enterMode(mode == MODE_GUARD ? MODE_VERIFY : MODE_GUARD);
      }
    }
  }

  /* 按住即提示求助，松开确认，不必连按三次 */
  if (bootHeld && !helpArmed && millis() - bootDownAt >= 800) {
    helpArmed = true;
    lcdStatus("HELP", "HOLD..", COL_HELP);
  }

  if (pendingListen && millis() >= listenAt) {
    pendingListen = false;
    sendGuardAudio();
  }

  if (pendingShot && millis() >= shotAt) {
    pendingShot = false;
    sendPackPhoto();
  }

  if (millis() - lastBeat > 4000) {
    lastBeat = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    } else if (!pendingShot && !pendingListen && !bootHeld) {
      sendHeartbeat(false);
    }
    Serial.printf("[%s] %s | %s\n", modeName().c_str(), lastLevel.c_str(), lastAdvice.c_str());
  }
}
