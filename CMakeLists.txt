cmake_minimum_required(VERSION 3.10)
project(ProxyServer C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(proxy_server
    proxy_server.c
    proxy_parse.c
)

target_include_directories(proxy_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}) 

printf("Client is connected with port number: %d and ip address: %s\n", ntohs(client_addr.sin_port), ip_str); 

printf("Received %s request for %s:%s%s\n", request->method, request->host, request->port ? request->port : "80", request->path); 

if (ParsedRequest_parse(request, buffer, len) < 0) { ... } 