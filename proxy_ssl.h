#ifndef PROXY_SSL_H
#define PROXY_SSL_H

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <winsock2.h>

// SSL context structure
typedef struct
{
    SSL_CTX *ctx;
    SSL *ssl;
    int is_initialized;
} SSLContext;

// Function declarations
int init_ssl_context(SSLContext *ssl_ctx);
void cleanup_ssl_context(SSLContext *ssl_ctx);
int setup_ssl_connection(SSLContext *ssl_ctx, SOCKET socket);
int ssl_send(SSLContext *ssl_ctx, const char *buffer, int length);
int ssl_recv(SSLContext *ssl_ctx, char *buffer, int length);

#endif // PROXY_SSL_H