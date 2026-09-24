/****************************************************************************
 * app/jianwei/jianwei_mic.c
 * SPDX-License-Identifier: Apache-2.0
 *
 * Prefer a raw read from /dev/audio/pcm_in0 (EYE PDM). If the audio
 * framework wants ioctls instead, we still return a skip rather than hang.
 ****************************************************************************/

#include "jianwei.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MIC_RATE 16000
#define MIC_SEC 8

static void wav_header(uint8_t *p, uint32_t pcm_len)
{
  uint32_t riff = 36 + pcm_len;
  uint32_t br = MIC_RATE * 2;
  uint32_t fmt_len = 16;
  uint16_t pcm = 1;
  uint16_t ch = 1;
  uint16_t bps = 16;
  uint16_t ba = 2;
  uint32_t rate = MIC_RATE;
  memcpy(p, "RIFF", 4);
  memcpy(p + 4, &riff, 4);
  memcpy(p + 8, "WAVEfmt ", 8);
  memcpy(p + 16, &fmt_len, 4);
  memcpy(p + 20, &pcm, 2);
  memcpy(p + 22, &ch, 2);
  memcpy(p + 24, &rate, 4);
  memcpy(p + 28, &br, 4);
  memcpy(p + 32, &ba, 2);
  memcpy(p + 34, &bps, 2);
  memcpy(p + 36, "data", 4);
  memcpy(p + 40, &pcm_len, 4);
}

int jianwei_send_listen(struct jianwei_advice *out)
{
  printf("listen talk %ds, then cloud\n", MIC_SEC);
  jianwei_show("GUARD", "TALK");

  int fd = open("/dev/audio/pcm_in0", O_RDONLY);
  if (fd < 0)
    {
      printf("listen skip errno=%d\n", errno);
      jianwei_show("GUARD", "NO MIC");
      return -1;
    }

  uint32_t pcm_len = MIC_RATE * 2 * MIC_SEC;
  uint8_t *wav = (uint8_t *)malloc(44 + pcm_len);
  if (wav == NULL)
    {
      close(fd);
      jianwei_show("GUARD", "MEM FAIL");
      return -1;
    }

  wav_header(wav, pcm_len);
  uint32_t got = 0;
  int idle = 0;
  while (got < pcm_len && idle < 40)
    {
      ssize_t n = read(fd, wav + 44 + got, pcm_len - got);
      if (n <= 0)
        {
          idle++;
          usleep(50 * 1000);
          continue;
        }
      idle = 0;
      got += (uint32_t)n;
    }
  close(fd);

  if (got < 1000)
    {
      printf("listen empty got=%u\n", (unsigned)got);
      free(wav);
      jianwei_show("GUARD", "MIC FAIL");
      return -1;
    }

  got -= (got % 2);
  wav_header(wav, got);
  printf("listen wav %u\n", (unsigned)(44 + got));

  char resp[2048];
  int code = jianwei_post_multipart("/api/guard-audio", "jwguard9k2",
                                    "audio", "guard.wav", "audio/wav",
                                    wav, 44 + got, resp, sizeof(resp));
  free(wav);
  if (code != 200)
    {
      printf("listen fail %d\n", code);
      jianwei_show("GUARD", "ASR FAIL");
      return -1;
    }

  char *json = strstr(resp, "{");
  if (json && out)
    {
      jianwei_parse_result(json, out);
    }
  printf("listen 200 level=%s\n",
         out && out->level[0] ? out->level : "-");
  return 0;
}
