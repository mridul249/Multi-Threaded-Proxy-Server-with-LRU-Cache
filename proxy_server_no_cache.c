// proxy_server_no_cache.c
#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>

#define BUF_SIZE 8192

// Logging Macro
#define LOG(fmt, ...)                                                                                          \
    do                                                                                                         \
    {                                                                                                          \
        FILE *log_fp = fopen("proxy_server_log.txt", "a");                                                     \
        if (log_fp)                                                                                            \
        {                                                                                                      \
            time_t now = time(NULL);                                                                           \
            struct tm *tm_info = localtime(&now);                                                              \
            char ts[32];                                                                                       \
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);                                            \
            fprintf(log_fp, "[%s] [PID:%d] [TID:%lu] " fmt "\n", ts, getpid(), pthread_self(), ##__VA_ARGS__); \
            fclose(log_fp);                                                                                    \
        }                                                                                                      \
    } while (0)

// Function Prototypes
int connect_to_host(const char *hostname, int port);
void relay_loop(int fd1, int fd2);
void *worker_thread(void *arg);

//
// Helpers
//
int connect_to_host(const char *hostname, int port)
{
    struct addrinfo hints, *res;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, port_str, &hints, &res) != 0)
    {
        perror("getaddrinfo");
        return -1;
    }

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0)
    {
        perror("socket");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0)
    {
        perror("connect");
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return sockfd;
}

void relay_loop(int fd1, int fd2)
{
    char buf[BUF_SIZE];
    fd_set fds;
    int maxfd = (fd1 > fd2 ? fd1 : fd2) + 1;
    LOG("Starting relay loop between fd=%d and fd=%d", fd1, fd2);

    while (1)
    {
        FD_ZERO(&fds);
        FD_SET(fd1, &fds);
        FD_SET(fd2, &fds);

        if (select(maxfd, &fds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            break;
        }

        if (FD_ISSET(fd1, &fds))
        {
            int n = recv(fd1, buf, BUF_SIZE, 0);
            if (n <= 0)
                break;
            if (send(fd2, buf, n, 0) < 0)
                break;
        }

        if (FD_ISSET(fd2, &fds))
        {
            int n = recv(fd2, buf, BUF_SIZE, 0);
            if (n <= 0)
                break;
            if (send(fd1, buf, n, 0) < 0)
                break;
        }
    }
    LOG("Relay loop ended (fd1=%d, fd2=%d)", fd1, fd2);
}

//
// Worker Thread
//
void *worker_thread(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);
    LOG("Handling client fd=%d", client_fd);

    char buf[BUF_SIZE];
    int total = 0;
    while (total < BUF_SIZE - 1)
    {
        int n = recv(client_fd, buf + total, BUF_SIZE - 1 - total, 0);
        if (n <= 0)
            break;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n"))
            break;
    }
    int n = total;

    LOG("Received request from fd=%d:\n%s", client_fd, buf);

    if (n <= 0)
    {
        LOG("Client fd=%d disconnected immediately", client_fd);
        close(client_fd);
        return NULL;
    }

    // --- HTTPS (CONNECT) Handling ---
    if (strncmp(buf, "CONNECT", 7) == 0)
    {
        char host[256];
        int port = 443;
        // Parse "CONNECT host:port HTTP/1.1"
        if (sscanf(buf, "CONNECT %255[^:]:%d", host, &port) < 1)
        {
            LOG("Invalid CONNECT request line from fd=%d", client_fd);
            close(client_fd);
            return NULL;
        }

        LOG("CONNECT request to %s:%d", host, port);
        int server_fd = connect_to_host(host, port);
        if (server_fd < 0)
        {
            LOG("Failed to connect for CONNECT tunnel");
            close(client_fd);
            return NULL;
        }

        const char *ok_response = "HTTP/1.1 200 Connection Established\r\n\r\n";
        send(client_fd, ok_response, strlen(ok_response), 0);

        relay_loop(client_fd, server_fd);
        close(server_fd);
    }
    else
    {
        ParsedRequest *req = ParsedRequest_create();

        printf("-----------------\n");
        printf("Before Parsing:\n %s", buf);
        printf("-----------------\n");

        // 1. FIRST, parse the request to fill the 'req' object with the client's data.
        if (!req || ParsedRequest_parse(req, buf, n) < 0)
        {
            LOG("Failed to parse HTTP request from fd=%d", client_fd);
            if (req)
                ParsedRequest_destroy(req);
            close(client_fd);
            return NULL;
        }

        LOG("Parsed request: %s %s Host=%s Port=%s", req->method, req->path, req->host, req->port ? req->port : "80");

        int port = req->port ? atoi(req->port) : 80;

        int server_fd = connect_to_host(req->host, port);
        if (server_fd < 0)
        {
            ParsedRequest_destroy(req);
            close(client_fd);
            return NULL;
        }

        // Reconstruct and send the full request to the origin server
        size_t req_len = ParsedRequest_totalLen(req);
        char *full_req_buf = (char *)malloc(req_len + 1);

        if (!full_req_buf)
        {
            LOG("Malloc failed for request buffer");
            ParsedRequest_destroy(req);
            close(server_fd);
            close(client_fd);
            return NULL;
        }

        if (ParsedRequest_unparse(req, full_req_buf, req_len) < 0)
        {
            LOG("Failed to unparse request");
        }
        else
        {
            printf("-----------------\n");
            printf("After Parsing:\n %s", full_req_buf);
            printf("-----------------\n");
            send(server_fd, full_req_buf, req_len, 0);
        }

        free(full_req_buf);
        relay_loop(client_fd, server_fd);
        close(server_fd);
        ParsedRequest_destroy(req);
    }

    LOG("Closing client fd=%d", client_fd);
    close(client_fd);
    return NULL;
}

//
// Main
//
int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        exit(1);
    }

    if (listen(server_fd, 20) < 0)
    {
        perror("listen");
        exit(1);
    }

    LOG("Proxy listening on port %d", port);

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        if (!client_fd)
        {
            continue;
        }

        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (*client_fd < 0)
        {
            perror("accept");
            free(client_fd);
            continue;
        }

        LOG("Accepted connection from %s:%d (fd=%d)",
            inet_ntoa(client_addr.sin_addr),
            ntohs(client_addr.sin_port),
            *client_fd);

        pthread_t tid;
        pthread_create(&tid, NULL, worker_thread, client_fd);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}