#include "proxy_ssl.h"
#include <stdio.h>

int init_ssl_context(SSLContext *ssl_ctx)
{
    // Initialize OpenSSL
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    SSL_library_init();

    // Create SSL context
    ssl_ctx->ctx = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx->ctx)
    {
        fprintf(stderr, "Error creating SSL context\n");
        return -1;
    }

    // Load certificate and private key
    if (SSL_CTX_use_certificate_file(ssl_ctx->ctx, "server.crt", SSL_FILETYPE_PEM) <= 0)
    {
        fprintf(stderr, "Error loading certificate\n");
        return -1;
    }

    if (SSL_CTX_use_PrivateKey_file(ssl_ctx->ctx, "server.key", SSL_FILETYPE_PEM) <= 0)
    {
        fprintf(stderr, "Error loading private key\n");
        return -1;
    }

    ssl_ctx->is_initialized = 1;
    return 0;
}

void cleanup_ssl_context(SSLContext *ssl_ctx)
{
    if (ssl_ctx->ssl)
    {
        SSL_free(ssl_ctx->ssl);
    }
    if (ssl_ctx->ctx)
    {
        SSL_CTX_free(ssl_ctx->ctx);
    }
    EVP_cleanup();
}

int setup_ssl_connection(SSLContext *ssl_ctx, SOCKET socket)
{
    if (!ssl_ctx->is_initialized)
    {
        return -1;
    }

    ssl_ctx->ssl = SSL_new(ssl_ctx->ctx);
    if (!ssl_ctx->ssl)
    {
        return -1;
    }

    if (SSL_set_fd(ssl_ctx->ssl, socket) != 1)
    {
        SSL_free(ssl_ctx->ssl);
        return -1;
    }

    if (SSL_accept(ssl_ctx->ssl) != 1)
    {
        SSL_free(ssl_ctx->ssl);
        return -1;
    }

    return 0;
}

int ssl_send(SSLContext *ssl_ctx, const char *buffer, int length)
{
    if (!ssl_ctx->ssl)
    {
        return -1;
    }
    return SSL_write(ssl_ctx->ssl, buffer, length);
}

int ssl_recv(SSLContext *ssl_ctx, char *buffer, int length)
{
    if (!ssl_ctx->ssl)
    {
        return -1;
    }
    return SSL_read(ssl_ctx->ssl, buffer, length);
}