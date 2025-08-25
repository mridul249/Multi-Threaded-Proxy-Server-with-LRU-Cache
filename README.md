# Multi-Threaded Proxy Server with LRU Cache

## 1\. Overall Architecture & System Flow

The system is designed as an **explicit, multi-threaded, non-caching TCP proxy server** for HTTP and HTTPS traffic. It operates as a middleman: clients (like web browsers or `curl`) are explicitly configured to send their web requests to this proxy, which then forwards them to the actual web servers on the internet.

### High-Level System Diagram

The flow is as follows:

1.  **Client Request**: A client, configured to use the proxy, initiates a TCP connection to the proxy server's listening port (e.g., 8080).
2.  **Proxy Server**: The main thread of the proxy server accepts this connection.
3.  **Thread Creation**: For each accepted client, the main thread spawns a new, detached worker thread to handle the entire lifecycle of that client's request. This isolates each client's session.
4.  **Request Handling**: The worker thread reads the client's request. It determines if it's an **HTTP request** (e.g., `GET`) or an **HTTPS tunnel request** (`CONNECT`).
5.  **Remote Connection**: The worker thread connects to the target server (e.g., `example.com`) on the appropriate port (80 for HTTP, 443 for HTTPS).
6.  **Data Relay**: The proxy relays data bidirectionally between the client and the target server.
7.  **Connection Teardown**: When either the client or the server closes the connection, the proxy cleans up by closing both sockets and terminating the worker thread.

-----

## 2\. Threading Model & Synchronization

The proxy uses a simple yet effective **thread-per-connection** model. This design choice prioritizes code simplicity and isolation over raw performance for a massive number of connections.

### Threading Model

  * **Main Thread**: Its sole responsibility is to listen for incoming connections on the server socket (`listen()`) and accept them (`accept()`).
  * **Worker Threads**: Upon accepting a new client, the main thread immediately creates a new worker thread using `pthread_create()`.
      * **Lifecycle**: Each worker thread is responsible for a single client connection from start to finish.
      * **Detached State**: Threads are created in a detached state using `pthread_detach()`. This is a crucial decision for a server, as it tells the operating system to automatically reclaim the thread's resources upon termination, avoiding the need for the main thread to `pthread_join()` each one. This prevents resource leaks and simplifies the main loop.

### Synchronization Strategy

In the provided *no-cache* version of the code, there are no shared mutable data structures (like a cache) that need protection. Therefore, **no explicit mutexes or locks are required** between threads. Each thread operates on its own set of file descriptors (`client_fd`, `server_fd`) and local buffers.

However, the conversation history indicates a desire to limit concurrent connections using a **semaphore**.

  * **Semaphore for Concurrency Control**: A POSIX semaphore (`sem_t`) is initialized with a maximum thread count (e.g., `MAX_CLIENTS`).
      * Before creating a new worker thread, the main thread calls `sem_wait()`, which blocks if the maximum number of threads is already running.
      * When a worker thread finishes its job and is about to exit, it must call `sem_post()` to signal that a slot has been freed up, allowing the main thread to accept another connection.

**Trade-offs**: The thread-per-connection model is easy to understand and debug but can be resource-intensive under heavy load, as each thread consumes memory for its stack. A more advanced architecture would use a thread pool or an event-driven model (like `epoll`).

-----

## 3\. Core Modules & Data Structures

The proxy's logic is primarily contained within the `worker_thread` function, supported by a few key helper modules for networking and parsing.

### Network I/O Handling

  * **Sockets**: Standard Berkeley sockets are used for all network communication. The server socket is created with `socket()`, bound with `bind()`, and set to listen with `listen()`. Client sockets are returned by `accept()`.
  * **Blocking I/O**: The initial `recv()` to get the request headers is a blocking call. This is a simple approach that works well because the client is expected to send its request immediately after connecting.
  * **Multiplexing with `select()`**: For the data relay phase (especially for HTTPS tunnels), the `select()` system call is used.
      * **Why `select()`?**: A simple `recv()` on one socket would block the thread, making it impossible to read from the other socket. `select()` allows the thread to monitor *both* the client and server file descriptors simultaneously and wake up only when one of them has data ready to be read. This enables **bidirectional, non-blocking data forwarding**.

### Request Parsing & Handling

The proxy intelligently distinguishes between HTTP and HTTPS traffic based on the initial request line. This is the most complex part of the `worker_thread`.

  * **Initial Read**: The thread performs a single `recv()` to get the client's request headers into a buffer.
  * **HTTPS (`CONNECT`) Logic**:
      * The buffer is checked to see if it starts with the string `"CONNECT"`. This check is performed *before* attempting to use the `proxy_parse` library, as the library was identified as not supporting the `CONNECT` method.
      * If it is a `CONNECT` request, the host and port are parsed manually from the request line using `sscanf()`.
      * The proxy connects to the remote server.
      * A successful `HTTP/1.1 200 Connection Established` response is sent back to the client.
      * The `tunnel_loop()` (or `relay_loop()`) is initiated to blindly forward encrypted TLS traffic.
  * **HTTP (`GET`, etc.) Logic**:
      * If the request is not `CONNECT`, it is passed to your `proxy_parse` library.
      * **Parsing**: The code calls `ParsedRequest_create()` and `ParsedRequest_parse()` to populate a `ParsedRequest` struct with the method, host, path, etc.
      * **Request Rewriting**: A critical step is rewriting the request line. Clients send a full URI to the proxy (e.g., `GET http://example.com/`), but the proxy must send a relative path to the origin server (e.g., `GET /`). The code constructs a new request line for this purpose.
      * **Header Forwarding**: The full set of headers from the parsed request object is reconstructed using `ParsedRequest_unparse_headers()` and sent to the target server after the rewritten request line.

### Data Structures

  * **`ParsedRequest` Struct**: Defined in `proxy_parse.h`, this is the central data structure for handling HTTP requests. It holds pointers to the various components of an HTTP request (method, host, path, headers, etc.).
  * **`fd_set`**: Used by the `select()` call in the `relay_loop` to manage the set of file descriptors (client and server) being monitored for readability.

-----

## 4\. Error Handling & Robustness

The server implements basic but essential error handling.

  * **System Call Checks**: All critical system calls (`socket`, `bind`, `listen`, `accept`, `connect`, `malloc`, `pthread_create`) are checked for failure (e.g., a return value of -1). On failure, an error message is printed to `stderr` using `perror()`, and the program or thread exits gracefully.
  * **Connection Handling**: The `relay_loop` terminates when `recv()` returns 0 (indicating a clean shutdown by the peer) or a value less than 0 (an error). This ensures the loop doesn't run forever on a dead connection.
  * **Logging**: A `LOG()` macro is used to print timestamped messages to `stderr`. This provides visibility into key events:
      * Server startup.
      * Accepting a new connection, including the client's IP and port.
      * The parsed request details (method, host, port).
      * The start and end of the relay loop.
      * Connection closures.

**Limitations**: The current error handling is simple. It does not send proper HTTP error codes (like 502 Bad Gateway or 400 Bad Request) back to the client in all failure scenarios, which a production proxy would do.

-----

## 5\. Architectural Flow Analysis (Data Flow Diagram)

This diagram illustrates the complete lifecycle of a client request through the proxy.

1.  **Listen & Accept**: The `main()` thread loops on `accept()`.

2.  **Dispatch**: A client connects. `accept()` returns `client_fd`. `main()` allocates memory for this fd, creates a `worker_thread`, and passes the pointer to it. The main thread immediately loops back to `accept()`.

3.  **Worker Starts**: The `worker_thread` starts, taking ownership of the `client_fd`.

4.  **Initial Read**: The worker calls `recv(client_fd, ...)` to get the request headers.

5.  **Branching Logic**:

      * **HTTPS Path (`CONNECT`)**:
          * `strncmp()` identifies a `CONNECT` request.
          * `sscanf()` parses the target `host` and `port`.
          * `connect_to_host()` establishes a connection to the target, creating `server_fd`.
          * `send(client_fd, "HTTP/1.1 200...")` is called.
          * Control passes to `relay_loop(client_fd, server_fd)`.
      * **HTTP Path (`GET`, etc.)**:
          * `ParsedRequest_parse()` is called to populate the `req` struct.
          * `connect_to_host()` uses `req->host` and `req->port` to create `server_fd`.
          * The request is reconstructed: `snprintf()` creates the path-only request line, and `ParsedRequest_unparse_headers()` prepares the headers.
          * `send(server_fd, ...)` transmits the full reconstructed request.
          * Control passes to `relay_loop(client_fd, server_fd)`.

6.  **Relay Phase**: Inside `relay_loop`:

      * `select()` monitors `client_fd` and `server_fd`.
      * If `client_fd` is readable, `recv()` reads from it and `send()` writes to `server_fd`.
      * If `server_fd` is readable, `recv()` reads from it and `send()` writes to `client_fd`.

7.  **Cleanup**: The `relay_loop` exits when a connection is closed. The worker thread then closes both `client_fd` and `server_fd`, destroys the `ParsedRequest` object, and terminates. If semaphores are used, it calls `sem_post()` before exiting.

8.  **Start the proxy server:**

    ```cmd
    proxy_server.exe 8080
    ```

    Replace `8080` with your desired port number.

9.  **Configure your browser:**

      - HTTP Proxy: `localhost:8080`
      - HTTPS Proxy: `localhost:8080`

### Alternative Build (using Make)

If you have `make` installed:

```cmd
make
proxy_server.exe 8080
```

## Usage

### Starting the Proxy Server

```bash
# Start proxy on port 8080
.\proxy_server.exe 8080

# Start proxy on port 3128
.\proxy_server.exe 3128
```

### Configuring Client Applications

#### Browser Configuration

1.  Open your browser's proxy settings
2.  Set HTTP proxy to: `localhost:8080` (or your chosen port)
3.  Set HTTPS proxy to: `localhost:8080`
4.  Save settings

#### Command Line Usage

```bash
# Using curl with the proxy
curl --proxy localhost:8080 http://example.com

# Using wget with the proxy
wget --http-proxy=localhost:8080 http://example.com
```

## Configuration

### Default Settings

  - **Default Port**: 8081 (if no port specified)
  - **Maximum Concurrent Connections**: 400
  - **Request Buffer Size**: 4096 bytes
  - **Cache Size**: 200 MB
  - **Maximum Cache Element Size**: 10 MB

### Modifying Settings

To change default settings, edit the constants in `proxy_server.c`:

```c
#define MAX_BYTES 4096           // Buffer size
#define MAX_CLIENTS 400          // Max concurrent connections
#define MAX_SIZE 200 * (1 << 20)     // Cache size (200MB)
#define MAX_ELEMENT_SIZE 10 * (1 << 20) // Max element size (10MB)
```

## How It Works

### HTTP GET Requests

1.  Client sends HTTP GET request to proxy
2.  Proxy checks cache for existing response
3.  If cached, returns cached response
4.  If not cached, forwards request to target server
5.  Stores response in cache and forwards to client

### HTTPS CONNECT Requests

1.  Client sends CONNECT request for HTTPS tunnel
2.  Proxy establishes connection to target server
3.  Returns "200 Connection Established" to client
4.  Starts bidirectional data forwarding (tunneling)
5.  Maintains tunnel until connection closes

### Cache Management

  - **Cache Hit**: Returns stored response immediately
  - **Cache Miss**: Fetches from server and stores in cache
  - **Cache Full**: Removes least recently used items
  - **Thread Safety**: Uses Windows Critical Sections

## File Structure

```
Proxy-Server/
├── proxy_server.c        # Main proxy server implementation
├── proxy_parse.c         # HTTP request parser
├── proxy_parse.h         # Parser header file
├── Makefile              # Build configuration
└── README.md             # This documentation
```

## Troubleshooting

### Common Issues

1.  **Port Already in Use**

      - Choose a different port number
      - Check for other proxy servers running

2.  **Connection Refused**

      - Verify firewall settings
      - Ensure proxy server is running
      - Check port configuration

3.  **Slow Performance**

      - Increase cache size
      - Check network connectivity
      - Monitor system resources

4.  **Memory Issues**

      - Reduce MAX\_CLIENTS value
      - Decrease cache size
      - Monitor memory usage

### Debug Information

The proxy server provides detailed console output including:

  - Client connection information
  - Request details
  - Cache hit/miss statistics
  - Error messages and codes

## Performance Considerations

  - **Threading**: Each client connection gets its own thread
  - **Semaphore**: Limits concurrent connections to prevent resource exhaustion
  - **Cache**: Reduces server load and improves response times
  - **Memory Management**: Efficient allocation and cleanup

## Limitations

  - Windows-specific implementation (uses Winsock2)
  - Supports only HTTP GET and HTTPS CONNECT methods
  - Cache is memory-based (not persistent)
  - No built-in logging to files

## Contributing

To contribute to this project:

1.  Fork the repository
2.  Create a feature branch
3.  Make your changes
4.  Test thoroughly
5.  Submit a pull request

## License

This project is open source and available under the MIT License.