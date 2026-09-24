/****************************************************************************
 * app/jianwei/jianwei_net.c
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "jianwei.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int extract_json_string(const char *json, const char *key,
                               char *dst, size_t n)
{
  char pat[64];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char *p = strstr(json, pat);
  if (p == NULL)
    {
      snprintf(pat, sizeof(pat), "\"%s\": \"", key);
      p = strstr(json, pat);
    }
  if (p == NULL)
    {
      return -1;
    }
  p += strlen(pat);
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < n)
    {
      dst[i++] = *p++;
    }
  dst[i] = 0;
  return 0;
}

void jianwei_parse_result(const char *json, struct jianwei_advice *out)
{
  if (json == NULL || out == NULL)
    {
      return;
    }
  extract_json_string(json, "level", out->level, sizeof(out->level));
  if (extract_json_string(json, "level_label", out->level_label,
                          sizeof(out->level_label)) != 0)
    {
      strncpy(out->level_label, out->level, sizeof(out->level_label) - 1);
    }
  extract_json_string(json, "advice", out->advice, sizeof(out->advice));
}

static int tcp_connect(void)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    {
      return -1;
    }

  struct timeval tv;
  tv.tv_sec = 90;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(JIANWEI_CLOUD_PORT);
  if (inet_pton(AF_INET, JIANWEI_CLOUD_HOST, &addr.sin_addr) != 1)
    {
      close(fd);
      return -1;
    }
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      close(fd);
      return -1;
    }
  return fd;
}

static int write_all(int fd, const void *p, size_t n)
{
  const uint8_t *b = (const uint8_t *)p;
  size_t off = 0;
  while (off < n)
    {
      ssize_t w = write(fd, b + off, n - off);
      if (w <= 0)
        {
          return -1;
        }
      off += (size_t)w;
    }
  return 0;
}

static int read_http(int fd, char *resp, size_t resp_n)
{
  size_t got = 0;
  while (got < resp_n - 1)
    {
      ssize_t r = read(fd, resp + got, resp_n - 1 - got);
      if (r <= 0)
        {
          break;
        }
      got += (size_t)r;
    }
  resp[got] = 0;

  int code = -1;
  if (strncmp(resp, "HTTP/", 5) == 0)
    {
      const char *sp = strchr(resp, ' ');
      if (sp)
        {
          code = atoi(sp + 1);
        }
    }
  return code;
}

int jianwei_http_post(const char *path, const char *content_type,
                      const uint8_t *body, size_t body_len,
                      char *resp, size_t resp_n)
{
  int fd = tcp_connect();
  if (fd < 0)
    {
      return -1;
    }

  char hdr[320];
  int n = snprintf(hdr, sizeof(hdr),
                   "POST %s HTTP/1.0\r\n"
                   "Host: %s:%d\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %u\r\n"
                   "Connection: close\r\n\r\n",
                   path, JIANWEI_CLOUD_HOST, JIANWEI_CLOUD_PORT,
                   content_type, (unsigned)body_len);
  if (write_all(fd, hdr, (size_t)n) < 0 ||
      (body_len > 0 && write_all(fd, body, body_len) < 0))
    {
      close(fd);
      return -1;
    }

  int code = read_http(fd, resp, resp_n);
  close(fd);
  return code;
}

int jianwei_post_multipart(const char *path, const char *bound,
                           const char *field, const char *filename,
                           const char *file_mime,
                           const uint8_t *data, size_t data_len,
                           char *resp, size_t resp_n)
{
  char head[256];
  char tail[80];
  int hl = snprintf(head, sizeof(head),
                    "--%s\r\nContent-Disposition: form-data; name=\"%s\"; "
                    "filename=\"%s\"\r\nContent-Type: %s\r\n\r\n",
                    bound, field, filename, file_mime);
  int tl = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", bound);
  size_t total = (size_t)hl + data_len + (size_t)tl;

  int fd = tcp_connect();
  if (fd < 0)
    {
      return -1;
    }

  char ctype[96];
  snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", bound);

  char hdr[320];
  int n = snprintf(hdr, sizeof(hdr),
                   "POST %s HTTP/1.0\r\n"
                   "Host: %s:%d\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %u\r\n"
                   "Connection: close\r\n\r\n",
                   path, JIANWEI_CLOUD_HOST, JIANWEI_CLOUD_PORT,
                   ctype, (unsigned)total);
  if (write_all(fd, hdr, (size_t)n) < 0 ||
      write_all(fd, head, (size_t)hl) < 0 ||
      write_all(fd, data, data_len) < 0 ||
      write_all(fd, tail, (size_t)tl) < 0)
    {
      close(fd);
      return -1;
    }

  int code = read_http(fd, resp, resp_n);
  close(fd);
  return code;
}

int jianwei_heartbeat(enum jianwei_mode mode, bool help,
                      struct jianwei_advice *out)
{
  char body[128];
  snprintf(body, sizeof(body),
           "{\"mode\":\"%s\",\"help\":%s}",
           mode == JIANWEI_VERIFY ? "verify" : "guard",
           help ? "true" : "false");

  char resp[1536];
  int code = jianwei_http_post("/api/device/heartbeat", "application/json",
                               (const uint8_t *)body, strlen(body),
                               resp, sizeof(resp));
  if (code != 200)
    {
      printf("beat fail %d\n", code);
      return -1;
    }

  char *json = strstr(resp, "{");
  if (json && out)
    {
      jianwei_parse_result(json, out);
    }
  printf("beat 200 mode=%s level=%s\n",
         mode == JIANWEI_VERIFY ? "verify" : "guard",
         out && out->level_label[0] ? out->level_label : "-");
  return 0;
}
