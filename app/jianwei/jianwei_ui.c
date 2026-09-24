/****************************************************************************
 * app/jianwei/jianwei_ui.c
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-S3-EYE exposes ST7789 via /dev/fb0 (not /dev/lcd0).
 ****************************************************************************/

#include "jianwei.h"

#include <errno.h>
#include <fcntl.h>
#include <nuttx/video/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>

#define COL_BG     0x1082u
#define COL_TEXT   0xffffu
#define COL_HIGH   0xf800u
#define COL_MID    0xfd20u
#define COL_LOW    0x07e0u
#define COL_HELP   0xf81fu
#define COL_WAIT   0xffe0u
#define COL_VERIFY 0x001fu

/* 8x8 glyphs: space, 0-9, A-Z */
static const uint8_t g_font8[][8] =
{
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0x3c, 0x66, 0x6e, 0x76, 0x66, 0x66, 0x3c, 0},
  {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7e, 0},
  {0x3c, 0x66, 0x06, 0x1c, 0x30, 0x66, 0x7e, 0},
  {0x3c, 0x66, 0x06, 0x1c, 0x06, 0x66, 0x3c, 0},
  {0x0c, 0x1c, 0x3c, 0x6c, 0x7e, 0x0c, 0x0c, 0},
  {0x7e, 0x60, 0x7c, 0x06, 0x06, 0x66, 0x3c, 0},
  {0x1c, 0x30, 0x60, 0x7c, 0x66, 0x66, 0x3c, 0},
  {0x7e, 0x66, 0x0c, 0x18, 0x18, 0x18, 0x18, 0},
  {0x3c, 0x66, 0x66, 0x3c, 0x66, 0x66, 0x3c, 0},
  {0x3c, 0x66, 0x66, 0x3e, 0x06, 0x0c, 0x38, 0},
  {0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0},
  {0x7c, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x7c, 0},
  {0x3c, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3c, 0},
  {0x78, 0x6c, 0x66, 0x66, 0x66, 0x6c, 0x78, 0},
  {0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x7e, 0},
  {0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x60, 0},
  {0x3c, 0x66, 0x60, 0x6e, 0x66, 0x66, 0x3c, 0},
  {0x66, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x66, 0},
  {0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e, 0},
  {0x06, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3c, 0},
  {0x66, 0x6c, 0x78, 0x70, 0x78, 0x6c, 0x66, 0},
  {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7e, 0},
  {0x63, 0x77, 0x7f, 0x6b, 0x63, 0x63, 0x63, 0},
  {0x66, 0x76, 0x7e, 0x7e, 0x6e, 0x66, 0x66, 0},
  {0x3c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0},
  {0x7c, 0x66, 0x66, 0x7c, 0x60, 0x60, 0x60, 0},
  {0x3c, 0x66, 0x66, 0x66, 0x6a, 0x6c, 0x36, 0},
  {0x7c, 0x66, 0x66, 0x7c, 0x6c, 0x66, 0x66, 0},
  {0x3c, 0x66, 0x60, 0x3c, 0x06, 0x66, 0x3c, 0},
  {0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0},
  {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0},
  {0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x18, 0},
  {0x63, 0x63, 0x63, 0x6b, 0x7f, 0x77, 0x63, 0},
  {0x66, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0x66, 0},
  {0x66, 0x66, 0x66, 0x3c, 0x18, 0x18, 0x18, 0},
  {0x7e, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x7e, 0},
};

static int glyph_index(char c)
{
  if (c == ' ')
    {
      return 0;
    }

  if (c >= '0' && c <= '9')
    {
      return 1 + (c - '0');
    }

  if (c >= 'A' && c <= 'Z')
    {
      return 11 + (c - 'A');
    }

  if (c >= 'a' && c <= 'z')
    {
      return 11 + (c - 'a');
    }

  return 0;
}

static uint16_t pick_bg(const char *l1, const char *l2)
{
  if (l1 && strncmp(l1, "HELP", 4) == 0)
    {
      return COL_HELP;
    }

  if (l1 && strncmp(l1, "VERIFY", 6) == 0)
    {
      return COL_VERIFY;
    }

  if (l2)
    {
      if (strstr(l2, "HIGH") || strstr(l2, "FAIL"))
        {
          return COL_HIGH;
        }

      if (strstr(l2, "CHECK") || strstr(l2, "AIM") || strstr(l2, "TALK") ||
          strstr(l2, "WAIT") || strstr(l2, "HOLD"))
        {
          return COL_WAIT;
        }

      if (strstr(l2, "OK") || strstr(l2, "LOW"))
        {
          return COL_LOW;
        }
    }

  return COL_BG;
}

static void fb_put_pixel(uint8_t *fb, unsigned stride, unsigned bpp,
                         unsigned x, unsigned y, uint16_t rgb565)
{
  if (bpp != 16)
    {
      return;
    }

  uint16_t *row = (uint16_t *)(fb + y * stride);
  row[x] = rgb565;
}

static void fb_fill(uint8_t *fb, unsigned w, unsigned h, unsigned stride,
                    unsigned bpp, uint16_t color)
{
  unsigned y;
  unsigned x;

  if (bpp != 16)
    {
      return;
    }

  /* Fast path: fill each row without per-pixel calls (keeps USB console alive). */
  for (y = 0; y < h; y++)
    {
      uint16_t *row = (uint16_t *)(fb + y * stride);
      for (x = 0; x < w; x++)
        {
          row[x] = color;
        }
    }
}

static void fb_draw_char(uint8_t *fb, unsigned w, unsigned h, unsigned stride,
                         unsigned bpp, int x, int y, char ch, uint16_t fg,
                         uint16_t bg, int scale)
{
  const uint8_t *g = g_font8[glyph_index(ch)];
  int r;
  int c;
  int sy;
  int sx;

  for (r = 0; r < 8; r++)
    {
      for (c = 0; c < 8; c++)
        {
          uint16_t color = (g[r] & (0x80 >> c)) ? fg : bg;
          for (sy = 0; sy < scale; sy++)
            {
              for (sx = 0; sx < scale; sx++)
                {
                  int px = x + c * scale + sx;
                  int py = y + r * scale + sy;
                  if (px >= 0 && py >= 0 &&
                      (unsigned)px < w && (unsigned)py < h)
                    {
                      fb_put_pixel(fb, stride, bpp, (unsigned)px,
                                   (unsigned)py, color);
                    }
                }
            }
        }
    }
}

static void fb_draw_text(uint8_t *fb, unsigned w, unsigned h, unsigned stride,
                         unsigned bpp, int y, const char *s, uint16_t fg,
                         uint16_t bg, int scale)
{
  int len;
  int total;
  int x;
  const char *p;

  if (!s)
    {
      s = "";
    }

  len = (int)strlen(s);
  if (len > 12)
    {
      len = 12;
    }

  total = len * 8 * scale;
  x = ((int)w - total) / 2;
  if (x < 0)
    {
      x = 0;
    }

  for (p = s; *p && p < s + len; p++)
    {
      fb_draw_char(fb, w, h, stride, bpp, x, y, *p, fg, bg, scale);
      x += 8 * scale;
    }
}

static int fb_draw_status(const char *l1, const char *l2, uint16_t bg)
{
  static int fd = -1;
  static uint8_t *fbmem = NULL;
  static size_t fblen = 0;
  static unsigned stride = 0;
  static unsigned bpp = 16;
  static unsigned xres = 240;
  static unsigned yres = 240;
  static int logged = 0;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  struct fb_area_s area;
  int ret;

  if (fd < 0)
    {
      fd = open("/dev/fb0", O_RDWR);
      if (fd < 0)
        {
          if (!logged)
            {
              printf("jianwei: open /dev/fb0 failed errno=%d\n", errno);
              logged = 1;
            }

          return -1;
        }

      ret = ioctl(fd, FBIOGET_VIDEOINFO,
                  (unsigned long)(uintptr_t)&vinfo);
      if (ret < 0)
        {
          printf("jianwei: FBIOGET_VIDEOINFO errno=%d\n", errno);
          close(fd);
          fd = -1;
          return -1;
        }

      ret = ioctl(fd, FBIOGET_PLANEINFO,
                  (unsigned long)(uintptr_t)&pinfo);
      if (ret < 0)
        {
          printf("jianwei: FBIOGET_PLANEINFO errno=%d\n", errno);
          close(fd);
          fd = -1;
          return -1;
        }

      xres = vinfo.xres;
      yres = vinfo.yres;
      stride = pinfo.stride;
      bpp = pinfo.bpp;
      fblen = pinfo.fblen;

      fbmem = mmap(NULL, fblen, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_FILE, fd, 0);
      if (fbmem == MAP_FAILED)
        {
          /* FLAT builds often expose pinfo.fbmem directly. */
          fbmem = (uint8_t *)pinfo.fbmem;
          if (fbmem == NULL)
            {
              printf("jianwei: mmap fb0 failed errno=%d\n", errno);
              close(fd);
              fd = -1;
              return -1;
            }
        }

      printf("jianwei: fb0 %ux%u bpp=%u stride=%u\n",
             xres, yres, bpp, stride);
    }

  if (bpp != 16)
    {
      printf("jianwei: fb0 bpp=%u unsupported\n", bpp);
      return -1;
    }

  fb_fill(fbmem, xres, yres, stride, bpp, bg);
  fb_draw_text(fbmem, xres, yres, stride, bpp, (int)yres / 4,
               l1 ? l1 : "", COL_TEXT, bg, 4);
  fb_draw_text(fbmem, xres, yres, stride, bpp, (int)yres / 2,
               l2 ? l2 : "", COL_TEXT, bg, 3);

#ifdef CONFIG_FB_UPDATE
  area.x = 0;
  area.y = 0;
  area.w = (fb_coord_t)xres;
  area.h = (fb_coord_t)yres;
  ioctl(fd, FBIO_UPDATE, (unsigned long)(uintptr_t)&area);
#else
  (void)area;
#endif

  return 0;
}

void jianwei_set_led(bool on)
{
  int fd = open("/dev/userleds", O_WRONLY);
  if (fd < 0)
    {
      return;
    }

  uint8_t v = on ? 1 : 0;
  write(fd, &v, 1);
  close(fd);
}

void jianwei_show(const char *l1, const char *l2)
{
  if (!l1)
    {
      l1 = "";
    }

  if (!l2)
    {
      l2 = "";
    }

  uint16_t bg = pick_bg(l1, l2);
  syslog(LOG_INFO, "jianwei: %s | %s\n", l1, l2);
  printf("[%s] %s\n", l1, l2);
  if (fb_draw_status(l1, l2, bg) < 0)
    {
      /* Keep serial UI even if panel path fails. */
    }
}

int jianwei_poll_boot(void)
{
  static int fd = -1;
  static int logged_fail = 0;

  if (fd < 0)
    {
      fd = open("/dev/buttons", O_RDONLY | O_NONBLOCK);
      if (fd < 0)
        {
          if (!logged_fail)
            {
              printf("jianwei: open /dev/buttons failed errno=%d\n", errno);
              logged_fail = 1;
            }

          return -1;
        }
    }

  uint32_t sample = 0;
  int n = read(fd, &sample, sizeof(sample));
  if (n < (int)sizeof(sample))
    {
      close(fd);
      fd = -1;
      return 0;
    }

  return (sample & 0x1u) ? 1 : 0;
}
