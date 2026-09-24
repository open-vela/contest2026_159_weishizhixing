/****************************************************************************
 * app/jianwei/jianwei_cam.c
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-S3-EYE OV2640 is QVGA RGB565 on /dev/video0. Convert to 24-bit BMP
 * so the cloud can PNG-encode and send to vision.
 ****************************************************************************/

#include "jianwei.h"

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef CONFIG_VIDEO
#include <nuttx/video/video.h>
#endif

#define JW_W 320
#define JW_H 240
#define JW_RGB565 (JW_W * JW_H * 2)

static void put_le32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void put_le16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static uint8_t *rgb565_to_bmp(const uint16_t *src, size_t *out_len)
{
  uint32_t px = JW_W * JW_H * 3;
  uint32_t total = 54 + px;
  uint8_t *bmp = (uint8_t *)malloc(total);
  if (bmp == NULL)
    {
      return NULL;
    }

  memset(bmp, 0, 54);
  bmp[0] = 'B';
  bmp[1] = 'M';
  put_le32(bmp + 2, total);
  put_le32(bmp + 10, 54);
  put_le32(bmp + 14, 40);
  put_le32(bmp + 18, JW_W);
  put_le32(bmp + 22, JW_H);
  put_le16(bmp + 26, 1);
  put_le16(bmp + 28, 24);

  for (int y = 0; y < JW_H; y++)
    {
      const uint16_t *row = src + (JW_H - 1 - y) * JW_W;
      uint8_t *d = bmp + 54 + y * JW_W * 3;
      for (int x = 0; x < JW_W; x++)
        {
          uint16_t p = row[x];
          int r = ((p >> 11) & 0x1f) * 255 / 31;
          int g = ((p >> 5) & 0x3f) * 255 / 63;
          int b = (p & 0x1f) * 255 / 31;
          d[x * 3 + 0] = (uint8_t)b;
          d[x * 3 + 1] = (uint8_t)g;
          d[x * 3 + 2] = (uint8_t)r;
        }
    }

  *out_len = total;
  return bmp;
}

#ifndef CONFIG_VIDEO
int jianwei_send_photo(struct jianwei_advice *out)
{
  (void)out;
  printf("photo skip (no CONFIG_VIDEO)\n");
  jianwei_show("VERIFY", "NO CAM");
  return -1;
}
#else

static int capture_rgb565(uint16_t **out)
{
  int fd = open("/dev/video0", O_RDWR);
  if (fd < 0)
    {
      printf("photo open video0 errno=%d\n", errno);
      return -1;
    }

  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = JW_W;
  fmt.fmt.pix.height = JW_H;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  if (ioctl(fd, VIDIOC_S_FMT, (unsigned long)&fmt) < 0)
    {
      printf("photo S_FMT errno=%d\n", errno);
      close(fd);
      return -1;
    }

  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_USERPTR;
  req.count = 1;
#ifdef V4L2_BUF_MODE_FIFO
  req.mode = V4L2_BUF_MODE_FIFO;
#endif
  if (ioctl(fd, VIDIOC_REQBUFS, (unsigned long)&req) < 0)
    {
      printf("photo REQBUFS errno=%d\n", errno);
      close(fd);
      return -1;
    }

  uint16_t *frame = (uint16_t *)memalign(32, JW_RGB565);
  if (frame == NULL)
    {
      close(fd);
      return -1;
    }

  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_USERPTR;
  buf.index = 0;
  buf.m.userptr = (unsigned long)frame;
  buf.length = JW_RGB565;
  if (ioctl(fd, VIDIOC_QBUF, (unsigned long)&buf) < 0)
    {
      printf("photo QBUF errno=%d\n", errno);
      free(frame);
      close(fd);
      return -1;
    }

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd, VIDIOC_STREAMON, (unsigned long)&type) < 0)
    {
      printf("photo STREAMON errno=%d\n", errno);
      free(frame);
      close(fd);
      return -1;
    }

  /* Drop one frame so AE can settle, then keep the next. */
  int i;
  for (i = 0; i < 2; i++)
    {
      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_USERPTR;
      if (ioctl(fd, VIDIOC_DQBUF, (unsigned long)&buf) < 0)
        {
          printf("photo DQBUF errno=%d\n", errno);
          ioctl(fd, VIDIOC_STREAMOFF, (unsigned long)&type);
          free(frame);
          close(fd);
          return -1;
        }
      if (i == 0)
        {
          ioctl(fd, VIDIOC_QBUF, (unsigned long)&buf);
        }
    }

  ioctl(fd, VIDIOC_STREAMOFF, (unsigned long)&type);
  close(fd);
  *out = frame;
  return 0;
}

int jianwei_send_photo(struct jianwei_advice *out)
{
  printf("photo capture\n");
  jianwei_show("VERIFY", "PHOTO");

  uint16_t *rgb = NULL;
  if (capture_rgb565(&rgb) < 0)
    {
      jianwei_show("VERIFY", "CAM FAIL");
      return -1;
    }

  size_t bmp_len = 0;
  uint8_t *bmp = rgb565_to_bmp(rgb, &bmp_len);
  free(rgb);
  if (bmp == NULL)
    {
      jianwei_show("VERIFY", "MEM FAIL");
      return -1;
    }

  printf("photo bmp %u\n", (unsigned)bmp_len);
  char resp[2048];
  int code = jianwei_post_multipart("/api/verify-image", "jwimg7k1",
                                    "image", "pack.bmp", "image/bmp",
                                    bmp, bmp_len, resp, sizeof(resp));
  free(bmp);
  if (code != 200)
    {
      printf("photo fail %d\n", code);
      jianwei_show("VERIFY", "UP FAIL");
      return -1;
    }

  char *json = strstr(resp, "{");
  if (json && out)
    {
      jianwei_parse_result(json, out);
    }
  printf("photo 200 level=%s\n",
         out && out->level[0] ? out->level : "-");
  return 0;
}
#endif
