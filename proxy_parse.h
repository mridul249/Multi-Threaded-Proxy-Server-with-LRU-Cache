#ifndef PROXY_PARSE_H
#define PROXY_PARSE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#define DEFAULT_NHDRS 8
#define DEBUG 0

// Corrected struct definition with typedef
typedef struct ParsedHeader {
    char *key;
    char *value;
    size_t keylen;
    size_t valuelen;
} ParsedHeader;

// Corrected struct definition with typedef
typedef struct ParsedRequest {
    char *buf;
    size_t buflen;

    char *method;
    char *host;
    char *port;
    char *path;
    char *version;

    ParsedHeader *headers;
    size_t headerslen;
    size_t headersused;
} ParsedRequest;

/* ParsedHeader functions */
void ParsedHeader_create(ParsedRequest *pr);
int ParsedHeader_set(ParsedRequest *pr, const char *key, const char *value);
ParsedHeader *ParsedHeader_get(ParsedRequest *pr, const char *key);
int ParsedHeader_remove(ParsedRequest *pr, const char *key);
size_t ParsedHeader_lineLen(ParsedHeader *ph);
size_t ParsedHeader_headersLen(ParsedRequest *pr);
int ParsedHeader_printHeaders(ParsedRequest *pr, char *buf, size_t len);
int ParsedHeader_parse(ParsedRequest *pr, char *line);
void ParsedHeader_destroy(ParsedRequest *pr);
void ParsedHeader_destroyOne(ParsedHeader *ph);

/* ParsedRequest functions */
ParsedRequest *ParsedRequest_create();
void ParsedRequest_destroy(ParsedRequest *pr);
int ParsedRequest_parse(ParsedRequest *pr, const char *buf, int buflen);
int ParsedRequest_unparse(ParsedRequest *pr, char *buf, size_t buflen);
int ParsedRequest_unparse_headers(ParsedRequest *pr, char *buf, size_t buflen);
size_t ParsedRequest_totalLen(ParsedRequest *pr);

#endif