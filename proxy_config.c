#include "proxy_config.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

void set_default_config(ProxyConfig *config)
{
    config->port = 8080;
    config->max_connections = 100;
    config->cache_size = 100 * 1024 * 1024; // 100MB
    config->enable_ssl = 0;
    config->enable_auth = 0;
    strcpy(config->ssl_cert_path, "server.crt");
    strcpy(config->ssl_key_path, "server.key");
    strcpy(config->cache_dir, "cache");
    config->log_level = 1; // 0=ERROR, 1=INFO, 2=DEBUG
}

int load_config(ProxyConfig *config, const char *config_file)
{
    FILE *file = fopen(config_file, "r");
    if (!file)
    {
        set_default_config(config);
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), file))
    {
        char key[256], value[256];
        if (sscanf(line, "%255[^=]=%255s", key, value) == 2)
        {
            if (strcmp(key, "port") == 0)
            {
                config->port = atoi(value);
            }
            else if (strcmp(key, "max_connections") == 0)
            {
                config->max_connections = atoi(value);
            }
            else if (strcmp(key, "cache_size") == 0)
            {
                config->cache_size = atoi(value);
            }
            else if (strcmp(key, "enable_ssl") == 0)
            {
                config->enable_ssl = atoi(value);
            }
            else if (strcmp(key, "enable_auth") == 0)
            {
                config->enable_auth = atoi(value);
            }
            else if (strcmp(key, "ssl_cert_path") == 0)
            {
                strncpy(config->ssl_cert_path, value, sizeof(config->ssl_cert_path) - 1);
            }
            else if (strcmp(key, "ssl_key_path") == 0)
            {
                strncpy(config->ssl_key_path, value, sizeof(config->ssl_key_path) - 1);
            }
            else if (strcmp(key, "cache_dir") == 0)
            {
                strncpy(config->cache_dir, value, sizeof(config->cache_dir) - 1);
            }
            else if (strcmp(key, "log_level") == 0)
            {
                config->log_level = atoi(value);
            }
        }
    }

    fclose(file);
    return 0;
}

int save_config(ProxyConfig *config, const char *config_file)
{
    FILE *file = fopen(config_file, "w");
    if (!file)
    {
        return -1;
    }

    fprintf(file, "port=%d\n", config->port);
    fprintf(file, "max_connections=%d\n", config->max_connections);
    fprintf(file, "cache_size=%d\n", config->cache_size);
    fprintf(file, "enable_ssl=%d\n", config->enable_ssl);
    fprintf(file, "enable_auth=%d\n", config->enable_auth);
    fprintf(file, "ssl_cert_path=%s\n", config->ssl_cert_path);
    fprintf(file, "ssl_key_path=%s\n", config->ssl_key_path);
    fprintf(file, "cache_dir=%s\n", config->cache_dir);
    fprintf(file, "log_level=%d\n", config->log_level);

    fclose(file);
    return 0;
}