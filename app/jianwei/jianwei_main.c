/****************************************************************************
 * app/jianwei/jianwei_main.c
 * SPDX-License-Identifier: Apache-2.0
 *
 * nsh> jianwei  (also auto-started from board bringup)
 *
 * Short BOOT: toggle verify/guard (UI first; no blocking TCP on press).
 * Long BOOT >=800ms: help family.
 * Risk judgement is cloud skills/ + LLM (openvela Skill-aligned).
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "jianwei.h"

static const char *risk_word(const struct jianwei_advice *adv)
{
  if (strcmp(adv->level, "high") == 0)
    {
      return "HIGH";
    }

  if (strcmp(adv->level, "mid") == 0)
    {
      return "CHECK";
    }

  if (strcmp(adv->level, "low") == 0)
    {
      return "OK";
    }

  return adv->level_label[0] ? adv->level_label : "--";
}

static void show_result(enum jianwei_mode mode, const struct jianwei_advice *adv)
{
  jianwei_show(mode == JIANWEI_VERIFY ? "VERIFY" : "GUARD", risk_word(adv));
}

static void enter_mode(enum jianwei_mode *mode, int *pending_shot,
                       int *pending_listen, int *shot_ms, int *listen_ms,
                       int *beat_ms)
{
  *mode = (*mode == JIANWEI_GUARD) ? JIANWEI_VERIFY : JIANWEI_GUARD;
  printf("mode -> %s\n", *mode == JIANWEI_VERIFY ? "verify" : "guard");
  jianwei_set_led(*mode == JIANWEI_VERIFY);

  /* Never call HTTP here — it dropped USB-ACM when BOOT was pressed. */
  *beat_ms = 3500; /* push a status beat soon after UI settles */

  if (*mode == JIANWEI_VERIFY)
    {
      *pending_listen = 0;
      if (access("/dev/video0", F_OK) == 0)
        {
          *pending_shot = 1;
          *shot_ms = 1000;
          jianwei_show("VERIFY", "AIM 1S");
        }
      else
        {
          *pending_shot = 0;
          jianwei_show("VERIFY", "READY");
        }
    }
  else
    {
      *pending_shot = 0;
      if (access("/dev/audio/pcm_in0", F_OK) == 0)
        {
          *pending_listen = 1;
          *listen_ms = 400;
          jianwei_show("GUARD", "TALK");
        }
      else
        {
          *pending_listen = 0;
          jianwei_show("GUARD", "READY");
        }
    }
}

int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  enum jianwei_mode mode = JIANWEI_GUARD;
  struct jianwei_advice adv;
  memset(&adv, 0, sizeof(adv));

  printf("jianwei running, cloud %s:%d\n",
         JIANWEI_CLOUD_HOST, JIANWEI_CLOUD_PORT);
  printf("BOOT: short=toggle, long hold=help\n");
  jianwei_show("JIANWEI", "GUARD");
  jianwei_set_led(false);

  int down_last = 0;
  int held_ms = 0;
  int help_armed = 0;
  int pending_help = 0;
  int beat_ms = 0;
  int pending_shot = 0;
  int pending_listen = 0;
  int shot_ms = 0;
  int listen_ms = 0;

  while (1)
    {
      int down = jianwei_poll_boot();
      if (down < 0)
        {
          down = 0;
        }

      if (down && !down_last)
        {
          held_ms = 0;
          help_armed = 0;
        }
      else if (down && down_last)
        {
          held_ms += 50;
          if (!help_armed && held_ms >= 800)
            {
              help_armed = 1;
              jianwei_show("HELP", "HOLD..");
            }
        }
      else if (!down && down_last)
        {
          if (help_armed || held_ms >= 800)
            {
              printf("HELP\n");
              pending_shot = 0;
              pending_listen = 0;
              strncpy(adv.level, "high", sizeof(adv.level) - 1);
              adv.level[sizeof(adv.level) - 1] = '\0';
              jianwei_set_led(true);
              jianwei_show("HELP", "FAMILY");
              pending_help = 1; /* HTTP after UI, not during press */
              beat_ms = 0;
            }
          else
            {
              printf("BOOT short held_ms=%d\n", held_ms);
              enter_mode(&mode, &pending_shot, &pending_listen,
                         &shot_ms, &listen_ms, &beat_ms);
            }

          held_ms = 0;
          help_armed = 0;
        }

      if (down != down_last)
        {
          printf("BOOT %s\n", down ? "down" : "up");
        }

      down_last = down;

      if (pending_help && !down)
        {
          pending_help = 0;
          jianwei_heartbeat(mode, true, &adv);
        }

      if (pending_listen)
        {
          listen_ms -= 50;
          if (listen_ms <= 0)
            {
              pending_listen = 0;
              if (jianwei_send_listen(&adv) == 0)
                {
                  show_result(mode, &adv);
                }
            }
        }

      if (pending_shot)
        {
          shot_ms -= 50;
          if (shot_ms <= 0)
            {
              pending_shot = 0;
              if (jianwei_send_photo(&adv) == 0)
                {
                  show_result(mode, &adv);
                }
            }
        }

      beat_ms += 50;
      if (beat_ms >= 4000)
        {
          beat_ms = 0;
          if (!pending_shot && !pending_listen && !down && !pending_help)
            {
              if (jianwei_heartbeat(mode, false, &adv) == 0 && adv.level[0])
                {
                  show_result(mode, &adv);
                }
            }
        }

      usleep(50 * 1000);
    }

  return 0;
}
