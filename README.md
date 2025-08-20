# Multi-Threaded Proxy Server with LRU Cache

A high-performance HTTP/HTTPS proxy server implemented in C with multi-threading support and LRU (Least Recently Used) cache functionality.

## Features

- **Multi-threaded Architecture**: Handles multiple client connections simultaneously (up to 400)
- **LRU Cache**: Intelligent caching system to improve response times (200MB cache)
- **HTTP/HTTPS Support**: HTTP GET requests with caching + HTTPS CONNECT tunneling
- **Windows Compatible**: Uses Winsock2 for Windows networking
- **Memory Management**: Efficient allocation and automatic cleanup
- **Error Handling**: Complete HTTP error responses (400, 403, 404, 500, 501, 502, 505)

## Quick Start

### Prerequisites
- Windows OS
- GCC compiler (MinGW recommended)
- Command prompt or PowerShell

### Build and Run

1. **Build the proxy server:**
   ```cmd
   gcc -g -Wall -std=c99 -o proxy_parse.o -c proxy_parse.c
   gcc -g -Wall -std=c99 -o proxy_server.o -c proxy_server.c
   gcc -g -Wall -std=c99 -o proxy_server.exe proxy_parse.o proxy_server.o -lws2_32
   ```

2. **Start the proxy server:**
   ```cmd
   proxy_server.exe 8080
   ```
   Replace `8080` with your desired port number.

3. **Configure your browser:**
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
1. Open your browser's proxy settings
2. Set HTTP proxy to: `localhost:8080` (or your chosen port)
3. Set HTTPS proxy to: `localhost:8080`
4. Save settings

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
#define MAX_BYTES 4096              // Buffer size
#define MAX_CLIENTS 400             // Max concurrent connections
#define MAX_SIZE 200 * (1 << 20)    // Cache size (200MB)
#define MAX_ELEMENT_SIZE 10 * (1 << 20) // Max element size (10MB)
```

## How It Works

### HTTP GET Requests
1. Client sends HTTP GET request to proxy
2. Proxy checks cache for existing response
3. If cached, returns cached response
4. If not cached, forwards request to target server
5. Stores response in cache and forwards to client

### HTTPS CONNECT Requests
1. Client sends CONNECT request for HTTPS tunnel
2. Proxy establishes connection to target server
3. Returns "200 Connection Established" to client
4. Starts bidirectional data forwarding (tunneling)
5. Maintains tunnel until connection closes

### Cache Management
- **Cache Hit**: Returns stored response immediately
- **Cache Miss**: Fetches from server and stores in cache
- **Cache Full**: Removes least recently used items
- **Thread Safety**: Uses Windows Critical Sections

## File Structure

```
Proxy-Server/
├── proxy_server.c          # Main proxy server implementation
├── proxy_parse.c           # HTTP request parser
├── proxy_parse.h           # Parser header file
├── Makefile               # Build configuration
└── README.md              # This documentation
```

## Troubleshooting

### Common Issues

1. **Port Already in Use**
   - Choose a different port number
   - Check for other proxy servers running

2. **Connection Refused**
   - Verify firewall settings
   - Ensure proxy server is running
   - Check port configuration

3. **Slow Performance**
   - Increase cache size
   - Check network connectivity
   - Monitor system resources

4. **Memory Issues**
   - Reduce MAX_CLIENTS value
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
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This project is open source and available under the MIT License.
