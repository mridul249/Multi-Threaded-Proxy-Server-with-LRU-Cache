#ifndef PROXY_AUTH_H
#define PROXY_AUTH_H

#include <winsock2.h>

// Authentication structure
typedef struct
{
    char username[64];
    char password[64];
    int is_authenticated;
} AuthContext;

// Function declarations
int init_auth_context(AuthContext *auth_ctx);
int authenticate_request(AuthContext *auth_ctx, const char *auth_header);
void cleanup_auth_context(AuthContext *auth_ctx);
char *generate_auth_response(void);

#endif // PROXY_AUTH_H