#ifndef PROXY_CONFIG_H
#define PROXY_CONFIG_H

#include <winsock2.h>

// Configuration structure
typedef struct
{
    int port;
    int max_connections;
    int cache_size;
    int enable_ssl;
    int enable_auth;
    char ssl_cert_path[256];
    char ssl_key_path[256];
    char cache_dir[256];
    int log_level;
} ProxyConfig;

// Function declarations
int load_config(ProxyConfig *config, const char *config_file);
int save_config(ProxyConfig *config, const char *config_file);
void set_default_config(ProxyConfig *config);

#endif // PROXY_CONFIG_H