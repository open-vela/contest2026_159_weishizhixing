/****************************************************************************
 * app/jianwei/jianwei.h
 * SPDX-License-Identifier: Apache-2.0
 *
 * Same HTTP paths and JSON fields as firmware/jianwei_eye.
 ****************************************************************************/

#ifndef JIANWEI_H
#define JIANWEI_H

#include <nuttx/config.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Prefer Kconfig CONFIG_JIANWEI_CLOUD_* when present. */
#ifdef CONFIG_JIANWEI_CLOUD_HOST
#  undef JIANWEI_CLOUD_HOST
#  define JIANWEI_CLOUD_HOST CONFIG_JIANWEI_CLOUD_HOST
#endif
#ifdef CONFIG_JIANWEI_CLOUD_PORT
#  undef JIANWEI_CLOUD_PORT
#  define JIANWEI_CLOUD_PORT CONFIG_JIANWEI_CLOUD_PORT
#endif

#ifndef JIANWEI_CLOUD_HOST
#  define JIANWEI_CLOUD_HOST "192.168.80.128"
#endif
#ifndef JIANWEI_CLOUD_PORT
#  define JIANWEI_CLOUD_PORT 8787
#endif

enum jianwei_mode
{
  JIANWEI_GUARD = 0,
  JIANWEI_VERIFY = 1
};

struct jianwei_advice
{
  char level[16];
  char level_label[32];
  char advice[160];
};

int jianwei_http_post(const char *path, const char *content_type,
                      const uint8_t *body, size_t body_len,
                      char *resp, size_t resp_n);

int jianwei_post_multipart(const char *path, const char *bound,
                           const char *field, const char *filename,
                           const char *file_mime,
                           const uint8_t *data, size_t data_len,
                           char *resp, size_t resp_n);

int jianwei_heartbeat(enum jianwei_mode mode, bool help,
                      struct jianwei_advice *out);

void jianwei_parse_result(const char *json, struct jianwei_advice *out);

int jianwei_poll_boot(void);
void jianwei_set_led(bool on);
void jianwei_show(const char *l1, const char *l2);

int jianwei_send_photo(struct jianwei_advice *out);
int jianwei_send_listen(struct jianwei_advice *out);

#endif
