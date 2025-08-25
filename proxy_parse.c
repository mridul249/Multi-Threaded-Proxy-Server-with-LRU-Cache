#include "proxy_parse.h"

#define MIN_REQ_LEN 4
#define MAX_REQ_LEN 65535
static const char *root_abs_path = "/";

void debug(const char *format, ...)
{
     va_list args;
     if (DEBUG)
     {
          va_start(args, format);
          vfprintf(stderr, format, args);
          va_end(args);
     }
}

/* Header functions */
void ParsedHeader_create(struct ParsedRequest *pr)
{
     pr->headers = (struct ParsedHeader *)malloc(sizeof(struct ParsedHeader) * DEFAULT_NHDRS);
     pr->headerslen = DEFAULT_NHDRS;
     pr->headersused = 0;
}

int ParsedHeader_set(struct ParsedRequest *pr, const char *key, const char *value)
{
     ParsedHeader_remove(pr, key);

     if (pr->headersused >= pr->headerslen)
     {
          pr->headerslen *= 2;
          pr->headers = (struct ParsedHeader *)realloc(pr->headers,
                                                       pr->headerslen * sizeof(struct ParsedHeader));
          if (!pr->headers)
               return -1;
     }

     struct ParsedHeader *ph = &pr->headers[pr->headersused++];
     ph->key = strdup(key);
     ph->value = strdup(value);
     ph->keylen = strlen(key) + 1;
     ph->valuelen = strlen(value) + 1;
     return 0;
}

struct ParsedHeader *ParsedHeader_get(struct ParsedRequest *pr, const char *key)
{
     for (size_t i = 0; i < pr->headersused; i++)
     {
          struct ParsedHeader *ph = &pr->headers[i];
          if (ph->key && strcmp(ph->key, key) == 0)
               return ph;
     }
     return NULL;
}

int ParsedHeader_remove(struct ParsedRequest *pr, const char *key)
{
     struct ParsedHeader *ph = ParsedHeader_get(pr, key);
     if (!ph)
          return -1;
     free(ph->key);
     free(ph->value);
     ph->key = ph->value = NULL;
     return 0;
}

size_t ParsedHeader_lineLen(struct ParsedHeader *ph)
{
     if (ph->key)
          return strlen(ph->key) + strlen(ph->value) + 4;
     return 0;
}

size_t ParsedHeader_headersLen(struct ParsedRequest *pr)
{
     size_t len = 0;
     for (size_t i = 0; i < pr->headersused; i++)
     {
          len += ParsedHeader_lineLen(&pr->headers[i]);
     }
     len += 2; // for \r\n at the end
     return len;
}

int ParsedHeader_printHeaders(struct ParsedRequest *pr, char *buf, size_t len)
{
     char *cur = buf;
     for (size_t i = 0; i < pr->headersused; i++)
     {
          struct ParsedHeader *ph = &pr->headers[i];
          if (!ph->key)
               continue;
          // Skip hop-by-hop headers
          if (strcasecmp(ph->key, "Proxy-Connection") == 0 ||
              strcasecmp(ph->key, "Connection") == 0 ||
              strcasecmp(ph->key, "Keep-Alive") == 0)
               continue;
          int n = snprintf(cur, len, "%s: %s\r\n", ph->key, ph->value);
          cur += n;
          len -= n;
     }
     snprintf(cur, len, "\r\n");
     return 0;
}

int ParsedHeader_parse(struct ParsedRequest *pr, char *line)
{
     char *sep = strchr(line, ':');
     if (!sep)
          return -1;
     *sep = '\0';
     char *key = line;
     char *value = sep + 1;
     while (*value == ' ')
          value++;
     return ParsedHeader_set(pr, key, value);
}

void ParsedHeader_destroyOne(struct ParsedHeader *ph)
{
     if (ph->key)
     {
          free(ph->key);
          ph->key = NULL;
     }
     if (ph->value)
     {
          free(ph->value);
          ph->value = NULL;
     }
}

void ParsedHeader_destroy(struct ParsedRequest *pr)
{
     for (size_t i = 0; i < pr->headersused; i++)
          ParsedHeader_destroyOne(&pr->headers[i]);
     free(pr->headers);
     pr->headers = NULL;
     pr->headersused = 0;
     pr->headerslen = 0;
}

/* ParsedRequest functions */
struct ParsedRequest *ParsedRequest_create()
{
     struct ParsedRequest *pr = (struct ParsedRequest *)malloc(sizeof(struct ParsedRequest));
     if (!pr)
          return NULL;
     ParsedHeader_create(pr);
     // AFTER
     pr->buf = pr->method = pr->host = pr->port = pr->path = pr->version = NULL;
     pr->buflen = 0;
     return pr;
}

// Corrected ParsedRequest_destroy function
void ParsedRequest_destroy(struct ParsedRequest *pr)
{
     if (!pr)
          return;

     // These were allocated with strdup in parse()
     if (pr->method)
          free(pr->method);
     if (pr->host)
          free(pr->host);
     if (pr->port)
          free(pr->port);
     if (pr->path)
          free(pr->path);
     if (pr->version)
          free(pr->version);

     // pr->buf is not used in this version, but good practice to keep
     if (pr->buf)
          free(pr->buf);

     // Clean up headers
     if (pr->headerslen > 0)
          ParsedHeader_destroy(pr);

     free(pr);
}
/* Parse HTTP request */
int ParsedRequest_parse(struct ParsedRequest *pr, const char *buf, int buflen)
{
     if (!pr || !buf || buflen < MIN_REQ_LEN)
          return -1;
     char *tmp = (char *)malloc(buflen + 1);
     memcpy(tmp, buf, buflen);
     tmp[buflen] = '\0';

     char *line_end = strstr(tmp, "\r\n");
     if (!line_end)
     {
          free(tmp);
          return -1;
     }
     *line_end = '\0';

     char *method = strtok(tmp, " ");
     char *url = strtok(NULL, " ");
     char *version = strtok(NULL, " ");

     if (!method || !url || !version)
     {
          free(tmp);
          return -1;
     }

     pr->method = strdup(method);
     pr->version = strdup(version);

     // parse host, port, path
     if (!strncmp(url, "http://", 7))
          url += 7;
     else if (!strncmp(url, "https://", 8))
          url += 8;

     char *path = strchr(url, '/');
     if (path)
     {
          pr->path = strdup(path);
          *path = '\0';
     }
     else
          pr->path = strdup("/");

     char *port = strchr(url, ':');
     if (port)
     {
          *port = '\0';
          port++;
          pr->port = strdup(port);
     }
     else
          pr->port = NULL;
     pr->host = strdup(url);

     // headers
     char *hdr_start = line_end + 2;
     char *hdr_end;
     while ((hdr_end = strstr(hdr_start, "\r\n")) && hdr_end != hdr_start)
     {
          *hdr_end = '\0';
          ParsedHeader_parse(pr, hdr_start);
          hdr_start = hdr_end + 2;
     }

     free(tmp);
     return 0;
}

/* Reconstruct HTTP request */
int ParsedRequest_unparse(struct ParsedRequest *pr, char *buf, size_t buflen)
{
     if (!pr || !buf)
          return -1;
     char *cur = buf;
     size_t n;

     n = snprintf(cur, buflen, "%s %s %s\r\n", pr->method, pr->path, pr->version);
     cur += n;
     buflen -= n;

     ParsedHeader_printHeaders(pr, cur, buflen);
     return 0;
}

size_t ParsedRequest_totalLen(struct ParsedRequest *pr)
{
     if (!pr)
          return 0;
     return strlen(pr->method) + strlen(pr->path) + strlen(pr->version) + 2 + ParsedHeader_headersLen(pr);
}

int ParsedRequest_unparse_headers(struct ParsedRequest *pr, char *buf, size_t buflen)
{
     if (!pr)
          return -1;
     return ParsedHeader_printHeaders(pr, buf, buflen);
}
