#include "proxy_auth.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <wincrypt.h>

// Default credentials (in production, these should be stored securely)
#define DEFAULT_USERNAME "admin"
#define DEFAULT_PASSWORD "proxy123"

int init_auth_context(AuthContext *auth_ctx)
{
    strncpy(auth_ctx->username, DEFAULT_USERNAME, sizeof(auth_ctx->username) - 1);
    strncpy(auth_ctx->password, DEFAULT_PASSWORD, sizeof(auth_ctx->password) - 1);
    auth_ctx->is_authenticated = 0;
    return 0;
}

int authenticate_request(AuthContext *auth_ctx, const char *auth_header)
{
    if (!auth_header)
    {
        return 0;
    }

    // Check if it's a Basic Auth header
    if (strncmp(auth_header, "Basic ", 6) != 0)
    {
        return 0;
    }

    // Decode base64 credentials
    char *encoded = (char *)auth_header + 6;
    DWORD decoded_size = 0;
    CryptStringToBinaryA(encoded, 0, CRYPT_STRING_BASE64, NULL, &decoded_size, NULL, NULL);

    if (decoded_size == 0)
    {
        return 0;
    }

    char *decoded = (char *)malloc(decoded_size + 1);
    if (!decoded)
    {
        return 0;
    }

    CryptStringToBinaryA(encoded, 0, CRYPT_STRING_BASE64, (BYTE *)decoded, &decoded_size, NULL, NULL);
    decoded[decoded_size] = '\0';

    // Split username and password
    char *colon = strchr(decoded, ':');
    if (!colon)
    {
        free(decoded);
        return 0;
    }

    *colon = '\0';
    char *username = decoded;
    char *password = colon + 1;

    // Verify credentials
    int authenticated = (strcmp(username, auth_ctx->username) == 0 &&
                         strcmp(password, auth_ctx->password) == 0);

    free(decoded);
    auth_ctx->is_authenticated = authenticated;
    return authenticated;
}

void cleanup_auth_context(AuthContext *auth_ctx)
{
    // Clear sensitive data
    SecureZeroMemory(auth_ctx->username, sizeof(auth_ctx->username));
    SecureZeroMemory(auth_ctx->password, sizeof(auth_ctx->password));
    auth_ctx->is_authenticated = 0;
}

char *generate_auth_response(void)
{
    return "WWW-Authenticate: Basic realm=\"Proxy Server\"\r\n";
}