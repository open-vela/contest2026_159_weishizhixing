#pragma once
/* ESP32-S3-EYE: OV2640 + ST7789 240x240。仅用 Arduino-ESP32 自带头文件。 */
#include <string.h>
#include "esp_camera.h"
#include "img_converters.h"
#include <SPI.h>
#if ESP_ARDUINO_VERSION_MAJOR >= 3
#include <ESP_I2S.h>
#else
#include <driver/i2s.h>
#endif

#define LCD_SCLK 21
#define LCD_MOSI 47
#define LCD_CS 44
#define LCD_DC 43
#define LCD_BL 48

static const uint16_t COL_BG = 0x1082;
static const uint16_t COL_TEXT = 0xFFFF;
static const uint16_t COL_HIGH = 0xF800;
static const uint16_t COL_MID = 0xFD20;
static const uint16_t COL_LOW = 0x07E0;
static const uint16_t COL_HELP = 0xF81F;
static const uint16_t COL_WAIT = 0xFFE0;

static SPIClass lcdSpi(FSPI);
static bool camReady = false;
static bool lcdReady = false;

/* 8x8 仅 0-9 A-Z 空格 */
static const uint8_t FONT8[][8] PROGMEM = {
    {0, 0, 0, 0, 0, 0, 0, 0},                 // sp
    {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0},  // 0
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0},
    {0x3C, 0x66, 0x06, 0x1C, 0x30, 0x66, 0x7E, 0},
    {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0},
    {0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0},
    {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0},
    {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0},
    {0x7E, 0x66, 0x0C, 0x18, 0x18, 0x18, 0x18, 0},
    {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0},
    {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0},  // 9
    {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0},  // A
    {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0},
    {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0},
    {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0},
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0},
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0},
    {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0},
    {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0},
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0},
    {0x06, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3C, 0},
    {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0},
    {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0},
    {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0},
    {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0},
    {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0},
    {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0},
    {0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0},
    {0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0},
    {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0},
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0},
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0},
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0},
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0},
    {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0},
    {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0},
    {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0},  // Z
};

static int glyphIndex(char c) {
  if (c == ' ') {
    return 0;
  }
  if (c >= '0' && c <= '9') {
    return 1 + (c - '0');
  }
  if (c >= 'A' && c <= 'Z') {
    return 11 + (c - 'A');
  }
  if (c >= 'a' && c <= 'z') {
    return 11 + (c - 'a');
  }
  return 0;
}

static void lcdCmd(uint8_t v) {
  digitalWrite(LCD_DC, LOW);
  digitalWrite(LCD_CS, LOW);
  lcdSpi.transfer(v);
  digitalWrite(LCD_CS, HIGH);
}

static void lcdDat(uint8_t v) {
  digitalWrite(LCD_DC, HIGH);
  digitalWrite(LCD_CS, LOW);
  lcdSpi.transfer(v);
  digitalWrite(LCD_CS, HIGH);
}

static void lcdWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  lcdCmd(0x2A);
  lcdDat(x0 >> 8);
  lcdDat(x0 & 0xFF);
  lcdDat(x1 >> 8);
  lcdDat(x1 & 0xFF);
  lcdCmd(0x2B);
  lcdDat(y0 >> 8);
  lcdDat(y0 & 0xFF);
  lcdDat(y1 >> 8);
  lcdDat(y1 & 0xFF);
  lcdCmd(0x2C);
}

static void lcdFill(uint16_t color) {
  if (!lcdReady) {
    return;
  }
  lcdWindow(0, 0, 239, 239);
  digitalWrite(LCD_DC, HIGH);
  digitalWrite(LCD_CS, LOW);
  for (int i = 0; i < 240 * 240; i++) {
    lcdSpi.transfer(color >> 8);
    lcdSpi.transfer(color & 0xFF);
  }
  digitalWrite(LCD_CS, HIGH);
}

static void lcdChar(int x, int y, char ch, uint16_t fg, uint16_t bg, int scale) {
  uint8_t g[8];
  memcpy_P(g, FONT8[glyphIndex(ch)], 8);
  int w = 8 * scale;
  lcdWindow(x, y, x + w - 1, y + w - 1);
  digitalWrite(LCD_DC, HIGH);
  digitalWrite(LCD_CS, LOW);
  for (int row = 0; row < 8; row++) {
    uint8_t bits = g[row];
    for (int sy = 0; sy < scale; sy++) {
      for (int col = 0; col < 8; col++) {
        uint16_t c = (bits & (0x80 >> col)) ? fg : bg;
        for (int sx = 0; sx < scale; sx++) {
          lcdSpi.transfer(c >> 8);
          lcdSpi.transfer(c & 0xFF);
        }
      }
    }
  }
  digitalWrite(LCD_CS, HIGH);
}

static void lcdText(int y, const char *s, uint16_t fg, uint16_t bg) {
  int n = 0;
  while (s[n]) {
    n++;
  }
  int scale = 5;
  if (n < 1) {
    return;
  }
  while (n * 8 * scale > 232 && scale > 2) {
    scale--;
  }
  int x = (240 - n * 8 * scale) / 2;
  if (x < 4) {
    x = 4;
  }
  while (*s && x + 8 * scale <= 240) {
    lcdChar(x, y, *s++, fg, bg, scale);
    x += 8 * scale;
  }
}

static void lcdStatus(const char *line1, const char *line2, uint16_t bg) {
  lcdFill(bg);
  lcdText(48, line1, COL_TEXT, bg);
  lcdText(130, line2, COL_TEXT, bg);
}

static void lcdBanner(const char *s, uint16_t bg) {
  if (!lcdReady) {
    return;
  }
  lcdWindow(0, 200, 239, 239);
  digitalWrite(LCD_DC, HIGH);
  digitalWrite(LCD_CS, LOW);
  for (int i = 0; i < 40 * 240; i++) {
    lcdSpi.transfer(bg >> 8);
    lcdSpi.transfer(bg & 0xFF);
  }
  digitalWrite(LCD_CS, HIGH);
  int n = 0;
  while (s && s[n]) {
    n++;
  }
  int scale = 3;
  int x = (240 - n * 8 * scale) / 2;
  if (x < 4) {
    x = 4;
  }
  while (s && *s && x + 8 * scale <= 240) {
    lcdChar(x, 208, *s++, COL_TEXT, bg, scale);
    x += 8 * scale;
  }
}

static bool lcdShowJpeg(const uint8_t *jpg, size_t len) {
  if (!lcdReady || !jpg || len < 100) {
    return false;
  }
  const int srcW = 320;
  const int srcH = 240;
  uint8_t *rgb = (uint8_t *)ps_malloc(srcW * srcH * 2);
  if (!rgb) {
    rgb = (uint8_t *)malloc(srcW * srcH * 2);
  }
  if (!rgb) {
    return false;
  }
  bool ok = jpg2rgb565(jpg, len, rgb, JPG_SCALE_2X);
  if (!ok) {
    free(rgb);
    return false;
  }
  lcdWindow(0, 0, 239, 239);
  digitalWrite(LCD_DC, HIGH);
  digitalWrite(LCD_CS, LOW);
  for (int y = 0; y < 240; y++) {
    const uint16_t *row = (const uint16_t *)(rgb + ((y * srcW) + 40) * 2);
    for (int x = 0; x < 240; x++) {
      uint16_t c = row[x];
      lcdSpi.transfer(c >> 8);
      lcdSpi.transfer(c & 0xFF);
    }
  }
  digitalWrite(LCD_CS, HIGH);
  free(rgb);
  return true;
}

#define SPK_PIN 38

static void spkPlayPcm16(const int16_t *pcm, size_t samples, int rate) {
  if (!pcm || samples < 16 || rate < 4000) {
    return;
  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(SPK_PIN, 32000, 8);
#else
  ledcSetup(4, 32000, 8);
  ledcAttachPin(SPK_PIN, 4);
#endif
  unsigned long us = 1000000UL / (unsigned long)rate;
  if (us < 20) {
    us = 20;
  }
  for (size_t i = 0; i < samples; i++) {
    uint8_t duty = (uint16_t)(pcm[i] + 32768) >> 8;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(SPK_PIN, duty);
#else
    ledcWrite(4, duty);
#endif
    delayMicroseconds(us);
  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(SPK_PIN, 128);
  ledcDetach(SPK_PIN);
#else
  ledcWrite(4, 128);
  ledcDetachPin(SPK_PIN);
#endif
}

static bool spkPlayWav(const uint8_t *d, size_t n) {
  if (!d || n < 44 || memcmp(d, "RIFF", 4) != 0) {
    return false;
  }
  size_t i = 12;
  uint16_t ch = 1, bits = 16;
  uint32_t rate = 16000;
  const uint8_t *data = nullptr;
  size_t dataLen = 0;
  while (i + 8 <= n) {
    uint32_t sz = (uint32_t)d[i + 4] | ((uint32_t)d[i + 5] << 8) | ((uint32_t)d[i + 6] << 16) |
                  ((uint32_t)d[i + 7] << 24);
    if (memcmp(d + i, "fmt ", 4) == 0 && i + 8 + 16 <= n) {
      ch = (uint16_t)d[i + 10] | ((uint16_t)d[i + 11] << 8);
      rate = (uint32_t)d[i + 12] | ((uint32_t)d[i + 13] << 8) | ((uint32_t)d[i + 14] << 16) |
             ((uint32_t)d[i + 15] << 24);
      bits = (uint16_t)d[i + 22] | ((uint16_t)d[i + 23] << 8);
    } else if (memcmp(d + i, "data", 4) == 0) {
      if (i + 8 + sz > n) {
        sz = n - (i + 8);
      }
      data = d + i + 8;
      dataLen = sz;
      break;
    }
    i += 8 + sz + (sz & 1);
  }
  if (!data || bits != 16 || ch < 1) {
    return false;
  }
  size_t frames = dataLen / (size_t)(2 * ch);
  if (frames < 16) {
    return false;
  }
  if (ch == 1) {
    spkPlayPcm16((const int16_t *)data, frames, (int)rate);
    return true;
  }
  int16_t *mono = (int16_t *)ps_malloc(frames * 2);
  if (!mono) {
    return false;
  }
  const int16_t *st = (const int16_t *)data;
  for (size_t f = 0; f < frames; f++) {
    mono[f] = st[f * ch];
  }
  spkPlayPcm16(mono, frames, (int)rate);
  free(mono);
  return true;
}

static void lcdBegin() {
  pinMode(LCD_CS, OUTPUT);
  pinMode(LCD_DC, OUTPUT);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_CS, HIGH);
  digitalWrite(LCD_BL, LOW);
  lcdSpi.begin(LCD_SCLK, -1, LCD_MOSI, LCD_CS);
  lcdSpi.beginTransaction(SPISettings(26000000, MSBFIRST, SPI_MODE0));
  lcdCmd(0x01);
  delay(120);
  lcdCmd(0x11);
  delay(120);
  lcdCmd(0x3A);
  lcdDat(0x55);
  lcdCmd(0x36);
  lcdDat(0x00);
  lcdCmd(0x21);
  lcdCmd(0x13);
  lcdCmd(0x29);
  delay(20);
  lcdReady = true;
  lcdStatus("JIANWEI", "BOOT", COL_BG);
}

static bool camBegin() {
  camera_config_t cfg = {};
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer = LEDC_TIMER_0;
  cfg.pin_d0 = 11;
  cfg.pin_d1 = 9;
  cfg.pin_d2 = 8;
  cfg.pin_d3 = 10;
  cfg.pin_d4 = 12;
  cfg.pin_d5 = 18;
  cfg.pin_d6 = 17;
  cfg.pin_d7 = 16;
  cfg.pin_xclk = 15;
  cfg.pin_pclk = 13;
  cfg.pin_vsync = 6;
  cfg.pin_href = 7;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  cfg.pin_sccb_sda = 4;
  cfg.pin_sccb_scl = 5;
  cfg.grab_mode = CAMERA_GRAB_LATEST;
#else
  cfg.pin_sscb_sda = 4;
  cfg.pin_sscb_scl = 5;
#endif
  cfg.pin_pwdn = -1;
  cfg.pin_reset = -1;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size = FRAMESIZE_VGA;
  cfg.jpeg_quality = 8;
  cfg.fb_count = 2;
  cfg.fb_location = CAMERA_FB_IN_PSRAM;
  esp_err_t err = esp_camera_init(&cfg);
  camReady = (err == ESP_OK);
  if (camReady) {
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
      s->set_vflip(s, 1);
      s->set_hmirror(s, 0);
      s->set_brightness(s, 2);
      s->set_saturation(s, 1);
    }
  }
  return camReady;
}

#define MIC_SCK 41
#define MIC_WS 42
#define MIC_SD 2
#define MIC_RATE 16000

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static I2SClass micI2s;
#endif
static bool micReady = false;

static void wavHeader(uint8_t *p, uint32_t pcmLen) {
  uint32_t riff = 36 + pcmLen;
  uint32_t br = MIC_RATE * 2;
  memcpy(p, "RIFF", 4);
  memcpy(p + 4, &riff, 4);
  memcpy(p + 8, "WAVEfmt ", 8);
  uint32_t fmtLen = 16;
  uint16_t pcm = 1, ch = 1, bps = 16, ba = 2;
  memcpy(p + 16, &fmtLen, 4);
  memcpy(p + 20, &pcm, 2);
  memcpy(p + 22, &ch, 2);
  uint32_t rate = MIC_RATE;
  memcpy(p + 24, &rate, 4);
  memcpy(p + 28, &br, 4);
  memcpy(p + 32, &ba, 2);
  memcpy(p + 34, &bps, 2);
  memcpy(p + 36, "data", 4);
  memcpy(p + 40, &pcmLen, 4);
}

static bool micBegin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  micI2s.setPins(MIC_SCK, MIC_WS, -1, MIC_SD);
  micReady = micI2s.begin(I2S_MODE_STD, MIC_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#else
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = MIC_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_desc_num = 8;
  cfg.dma_frame_num = 256;
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_GPIO_UNUSED;
  pins.bck_io_num = MIC_SCK;
  pins.ws_io_num = MIC_WS;
  pins.data_out_num = I2S_GPIO_UNUSED;
  pins.data_in_num = MIC_SD;
  micReady = i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL) == ESP_OK &&
             i2s_set_pin(I2S_NUM_1, &pins) == ESP_OK;
#endif
  return micReady;
}

static uint8_t *micCaptureWavUntilBoot(unsigned maxMs, size_t *outLen) {
  *outLen = 0;
  if (!micReady) {
    return nullptr;
  }
  uint32_t stereoBytes = MIC_RATE * 4 * 8;
  uint8_t *buf = (uint8_t *)ps_malloc(44 + stereoBytes);
  if (!buf) {
    buf = (uint8_t *)malloc(44 + stereoBytes);
  }
  if (!buf) {
    return nullptr;
  }
  wavHeader(buf, stereoBytes);
  unsigned long t0 = millis();
  while (digitalRead(0) == LOW && millis() - t0 < 800) {
    delay(10);
  }
  size_t got = 0;
  bool released = digitalRead(0) == HIGH;
  t0 = millis();
  while (got < stereoBytes && millis() - t0 < maxMs) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    int n = micI2s.readBytes((char *)(buf + 44 + got), stereoBytes - got);
    if (n <= 0) {
      delay(1);
    } else {
      got += (size_t)n;
    }
#else
    size_t n = 0;
    i2s_read(I2S_NUM_1, buf + 44 + got, stereoBytes - got, &n, 20 / portTICK_PERIOD_MS);
    got += n;
#endif
    int pin = digitalRead(0);
    if (pin == HIGH) {
      released = true;
    }
    if (released && pin == LOW && millis() - t0 > 600) {
      delay(25);
      if (digitalRead(0) == LOW) {
        Serial.println("listen stop btn");
        break;
      }
    }
  }
  got -= (got % 4);
  int16_t *st = (int16_t *)(buf + 44);
  uint32_t frames = got / 4;
  uint32_t eL = 1, eR = 1;
  for (uint32_t i = 0; i < frames; i++) {
    int a = st[i * 2];
    int b = st[i * 2 + 1];
    if (a < 0) {
      a = -a;
    }
    if (b < 0) {
      b = -b;
    }
    eL += (uint32_t)a;
    eR += (uint32_t)b;
  }
  bool useL = eL >= eR;
  for (uint32_t i = 0; i < frames; i++) {
    st[i] = useL ? st[i * 2] : st[i * 2 + 1];
  }
  uint32_t pcmLen = frames * 2;
  int mx = 1;
  for (uint32_t i = 0; i < frames; i++) {
    int a = st[i];
    if (a < 0) {
      a = -a;
    }
    if (a > mx) {
      mx = a;
    }
  }
  if (mx > 80) {
    int g = 14000 / mx;
    if (g < 2) {
      g = 2;
    }
    if (g > 24) {
      g = 24;
    }
    for (uint32_t i = 0; i < frames; i++) {
      int v = (int)st[i] * g;
      if (v > 32767) {
        v = 32767;
      }
      if (v < -32768) {
        v = -32768;
      }
      st[i] = (int16_t)v;
    }
  }
  wavHeader(buf, pcmLen);
  *outLen = 44 + pcmLen;
  return buf;
}
