#define _CRT_SECURE_NO_WARNINGS
#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <time.h>
#include <errno.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_BYTES 4096					// max allowed size of request/response
#define MAX_CLIENTS 400					// max number of client requests served at a time
#define MAX_SIZE 200 * (1 << 20)		// size of the cache
#define MAX_ELEMENT_SIZE 10 * (1 << 20) // max size of an element in cache

typedef struct cache_element cache_element;

struct cache_element
{
	char *data;			   // data stores response
	int len;			   // length of data i.e.. sizeof(data)...
	char *url;			   // url stores the request
	time_t lru_time_track; // lru_time_track stores the latest time the element is accessed
	cache_element *next;   // pointer to next element
};

// Function declarations
cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_element();
DWORD WINAPI thread_fn(LPVOID socketNew);

// Global variables
int port_number = 8081;				// Default Port
SOCKET proxy_socketId;				// socket descriptor of proxy server
HANDLE thread_handles[MAX_CLIENTS]; // array to store thread handles
HANDLE semaphore;					// semaphore for limiting concurrent connections
CRITICAL_SECTION cache_lock;		// critical section for cache access
cache_element *head = NULL;			// pointer to the cache
int cache_size = 0;					// current size of the cache

int sendErrorMessage(SOCKET socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);
	struct tm data;
	struct tm *tmp = gmtime(&now);
	if (tmp != NULL)
		data = *tmp;
	strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

	switch (status_code)
	{
	case 400:
		snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: WindowsProxy/1.0\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Request</H1>\n</BODY></HTML>", currentTime);
		printf("400 Bad Request\n");
		send(socket, str, strlen(str), 0);
		break;

	case 403:
		snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: WindowsProxy/1.0\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
		printf("403 Forbidden\n");
		send(socket, str, strlen(str), 0);
		break;

	case 404:
		snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: WindowsProxy/1.0\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
		printf("404 Not Found\n");
		send(socket, str, strlen(str), 0);
		break;

	case 500:
		snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: WindowsProxy/1.0\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
		send(socket, str, strlen(str), 0);
		break;

	case 501:
		snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: WindowsProxy/1.0\r\n\r\n<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
		printf("501 Not Implemented\n");
		send(socket, str, strlen(str), 0);
		break;

	case 505:
		snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: WindowsProxy/1.0\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
		printf("505 HTTP Version Not Supported\n");
		send(socket, str, strlen(str), 0);
		break;

	case 502:
		snprintf(str, sizeof(str), "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: WindowsProxy/1.0\r\n\r\n<HTML><HEAD><TITLE>502 Bad Gateway</TITLE></HEAD>\n<BODY><H1>502 Bad Gateway</H1>\n</BODY></HTML>", currentTime);
		printf("502 Bad Gateway\n");
		send(socket, str, strlen(str), 0);
		break;

	default:
		return -1;
	}
	return 1;
}

SOCKET connectRemoteServer(char *host_addr, int port_num)
{
	// Step 1: Creating a TCP socket
	SOCKET remoteSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	// AF_INET: value is 2 for specifying IPv4
	// SOCK_STREAM: value is 1 for specifying TCP
	// IPPROTO_TCP: value is 6 for specifying TCP
	if (remoteSocket == INVALID_SOCKET)
	{
		printf("Error in Creating Socket: %d\n", WSAGetLastError());
		return INVALID_SOCKET;
	}

	// Step 2: Prepare server address structure
	struct sockaddr_in server_addr;
	ZeroMemory(&server_addr, sizeof(server_addr)); // Initialize with zeros
	server_addr.sin_family = AF_INET;			   // Use IPv4
	server_addr.sin_port = htons(port_num);		   // The htons() function converts from host byte order to network byte order

	// Step 3: Perform DNS resolution to get the IP address of the hostname
	struct hostent *he = gethostbyname(host_addr);
	if (he == NULL)
	{
		printf("DNS resolution failed for %s\n", host_addr);
		closesocket(remoteSocket); // Cleanup
		return INVALID_SOCKET;
	}

	// Step 4: Copy the resolved IP address into the sockaddr_in struct
	// doing memcpy to copy the IP address from the hostent structure to the server_addr structure
	memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);

	// Step 5: Connect to the remote server using the socket
	if (connect(remoteSocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
	{
		printf("Error in connecting: %d\n", WSAGetLastError());
		closesocket(remoteSocket); // Cleanup
		return INVALID_SOCKET;
	}

	// Step 6: Return the connected socket
	return remoteSocket;
}

int handle_request(SOCKET clientSocket, ParsedRequest *request, char *tempReq)
{
	// Allocate buffer to build the HTTP request
	char *buf = (char *)malloc(sizeof(char) * MAX_BYTES);
	if (!buf)
	{
		printf("Failed to allocate buf\n");
		return -1;
	}

	// Construct the request line: "GET /path HTTP/1.1\r\n"
	strncpy(buf, "GET ", MAX_BYTES);
	strncat(buf, request->path, MAX_BYTES - strlen(buf) - 1);
	strncat(buf, " ", MAX_BYTES - strlen(buf) - 1);
	strncat(buf, request->version, MAX_BYTES - strlen(buf) - 1);
	strncat(buf, "\r\n", MAX_BYTES - strlen(buf) - 1);

	size_t len = strlen(buf); // Current buffer length

	// Set "Connection: close" header to avoid persistent connections
	if (ParsedHeader_set(request, "Connection", "close") < 0)
	{
		printf("set header key not work\n");
	}

	// Ensure "Host" header is present
	if (ParsedHeader_get(request, "Host") == NULL)
	{
		if (ParsedHeader_set(request, "Host", request->host) < 0)
		{
			printf("Set \"Host\" header key not working\n");
		}
	}

	// Append all remaining headers to the buffer
	if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0)
	{
		printf("unparse failed\n");
		free(buf);
		return -1;
	}

	// Determine server port (default = 80)
	int server_port = 80;
	if (request->port != NULL)
	{
		server_port = atoi(request->port);
	}

	// Connect to the remote server
	printf("Connecting to remote server: %s:%d\n", request->host, server_port);
	SOCKET remoteSocketID = connectRemoteServer(request->host, server_port);
	if (remoteSocketID == INVALID_SOCKET)
	{
		printf("Failed to connect to remote server\n");
		free(buf);
		return -1;
	}

	// Send the HTTP request to the remote server
	printf("Sending request to remote server...\n");
	int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);
	if (bytes_send < 0)
	{
		printf("Failed to send request to remote server\n");
		free(buf);
		closesocket(remoteSocketID);
		return -1;
	}
	free(buf); // Free request buffer

	// Allocate buffer to receive response
	char *temp_buffer = (char *)malloc(sizeof(char) * MAX_BYTES);
	if (!temp_buffer)
	{
		printf("Failed to allocate temp_buffer\n");
		closesocket(remoteSocketID);
		return -1;
	}

	int temp_buffer_index = 0;
	int bytes_recv;

	printf("Receiving response from remote server...\n");
	// Receive data from the server and forward it to the client
	while ((bytes_recv = recv(remoteSocketID, temp_buffer + temp_buffer_index, MAX_BYTES, 0)) > 0)
	{
		// Forward the received data to the client
		send(clientSocket, temp_buffer + temp_buffer_index, bytes_recv, 0);

		// Update buffer index
		temp_buffer_index += bytes_recv;

		// Resize buffer for more data
		temp_buffer = realloc(temp_buffer, temp_buffer_index + MAX_BYTES);
		if (!temp_buffer)
		{
			printf("Failed to realloc temp_buffer\n");
			closesocket(remoteSocketID);
			return -1;
		}
	}

	// Check if an error occurred while receiving
	if (bytes_recv < 0)
	{
		printf("Failed to receive response from remote server\n");
	}

	// Cache the complete response
	add_cache_element(temp_buffer, temp_buffer_index, tempReq);

	printf("Done\n");

	// Cleanup
	free(temp_buffer);
	closesocket(remoteSocketID);
	return 0;
}

int checkHTTPversion(char *msg)
{
	if (strncmp(msg, "HTTP/1.1", 8) == 0 || strncmp(msg, "HTTP/1.0", 8) == 0)
	{
		return 1;
	}
	return -1;
}

int handle_connect_request(SOCKET clientSocket, ParsedRequest *request)
{
	char *host = request->host;
	int port = 443; // Default HTTPS port

	if (request->port != NULL)
	{
		port = atoi(request->port);
	}

	printf("Connecting to HTTPS server: %s:%d\n", host, port);
	SOCKET remoteSocket = connectRemoteServer(host, port);
	if (remoteSocket == INVALID_SOCKET)
	{
		sendErrorMessage(clientSocket, 502); // Bad Gateway
		return -1;
	}

	// Send 200 Connection Established response
	char response[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
	if (send(clientSocket, response, strlen(response), 0) < 0)
	{
		closesocket(remoteSocket);
		return -1;
	}

	// Set up non-blocking mode for both sockets
	u_long mode = 1;
	ioctlsocket(clientSocket, FIONBIO, &mode);
	ioctlsocket(remoteSocket, FIONBIO, &mode);

	// Start bidirectional tunneling
	fd_set readfds;
	int max_fd = (clientSocket > remoteSocket) ? clientSocket : remoteSocket;
	struct timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;

	while (1)
	{
		FD_ZERO(&readfds);
		FD_SET(clientSocket, &readfds);
		FD_SET(remoteSocket, &readfds);

		int activity = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
		if (activity < 0)
		{
			break;
		}

		char buffer[MAX_BYTES];
		int bytes_read;

		// Client to Server
		if (FD_ISSET(clientSocket, &readfds))
		{
			bytes_read = recv(clientSocket, buffer, MAX_BYTES, 0);
			if (bytes_read <= 0)
			{
				break;
			}
			if (send(remoteSocket, buffer, bytes_read, 0) <= 0)
			{
				break;
			}
		}

		// Server to Client
		if (FD_ISSET(remoteSocket, &readfds))
		{
			bytes_read = recv(remoteSocket, buffer, MAX_BYTES, 0);
			if (bytes_read <= 0)
			{
				break;
			}
			if (send(clientSocket, buffer, bytes_read, 0) <= 0)
			{
				break;
			}
		}
	}

	// Restore blocking mode
	mode = 0;
	ioctlsocket(clientSocket, FIONBIO, &mode);
	ioctlsocket(remoteSocket, FIONBIO, &mode);

	closesocket(remoteSocket);
	return 0;
}

DWORD WINAPI thread_fn(LPVOID socketNew)
{
	WaitForSingleObject(semaphore, INFINITE);

	SOCKET socket = *(SOCKET *)socketNew;
	int bytes_send_client, len;

	char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
	if (!buffer)
	{
		ReleaseSemaphore(semaphore, 1, NULL);
		return 1;
	}

	ZeroMemory(buffer, MAX_BYTES);
	bytes_send_client = recv(socket, buffer, MAX_BYTES - 1, 0);

	if (bytes_send_client > 0)
	{
		len = strlen(buffer);
		if (strstr(buffer, "\r\n\r\n") == NULL)
		{
			if (len >= MAX_BYTES - 1)
			{
				printf("Request too large\n");
				free(buffer);
				ReleaseSemaphore(semaphore, 1, NULL);
				return 1;
			}
			bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len - 1, 0);
		}

		size_t req_len = strlen(buffer);
		char *tempReq = (char *)malloc(req_len + 1);
		if (!tempReq)
		{
			free(buffer);
			ReleaseSemaphore(semaphore, 1, NULL);
			return 1;
		}

		strncpy(tempReq, buffer, req_len);
		tempReq[req_len] = '\0';

		// Check for GET /example.com HTTP/1.1 with Host: localhost
		if (strncmp(buffer, "GET /", 5) == 0)
		{
			char *host_header = strstr(buffer, "Host: ");
			if (host_header &&
				(strstr(host_header, "localhost") || strstr(host_header, "127.0.0.1")))
			{
				// Extract the target host from the path
				char *path_start = buffer + 5;
				char *path_end = strchr(path_start, ' ');
				if (path_end)
				{
					size_t host_len = path_end - path_start;
					char target_host[256] = {0};
					strncpy(target_host, path_start, host_len);

					// Build a new request line
					char new_buffer[MAX_BYTES];
					snprintf(new_buffer, sizeof(new_buffer), "GET http://%s/ HTTP/1.1\r\nHost: %s\r\n\r\n", target_host, target_host);

					// Copy new_buffer back to buffer
					strncpy(buffer, new_buffer, MAX_BYTES - 1);
					buffer[MAX_BYTES - 1] = '\0';
					len = strlen(buffer);
				}
			}
		}

		ParsedRequest *request = ParsedRequest_create();
		if (ParsedRequest_parse(request, buffer, len) < 0)
		{
			printf("Parsing failed\n");
			ParsedRequest_destroy(request);
			free(buffer);
			free(tempReq);
			ReleaseSemaphore(semaphore, 1, NULL);
			return 1;
		}

		printf("Received %s request for %s:%s%s\n", request->method, request->host, request->port ? request->port : "80", request->path);

		// Special handling for GET /example.com HTTP/1.1 with Host: localhost
		if (strcmp(request->method, "GET") == 0 &&
			(strcmp(request->host, "localhost") == 0 || strcmp(request->host, "127.0.0.1") == 0) &&
			request->path && strlen(request->path) > 1 && request->path[0] == '/' &&
			strncmp(request->path, "//", 2) != 0 &&
			strncmp(request->path, "http", 4) != 0)
		{

			// Extract hostname from path
			char *target_host = request->path + 1; // skip leading '/'
			char *slash = strchr(target_host, '/');
			char *target_path = "/";
			if (slash)
			{
				*slash = '\0';
				target_path = slash + 1;
			}

			// Set up the request as if it was a proxy request
			free(request->host);
			request->host = strdup(target_host);
			free(request->path);
			request->path = strdup("/");
			request->port = NULL; // default to 80

			// Optionally, set the Host header
			ParsedHeader_set(request, "Host", request->host);

			printf("Rewritten request: GET http://%s/\n", request->host);
		}

		// Handle CONNECT method for HTTPS
		if (strcmp(request->method, "CONNECT") == 0)
		{
			if (request->host && checkHTTPversion(request->version) == 1)
			{
				if (handle_connect_request(socket, request) == -1)
				{
					sendErrorMessage(socket, 502);
				}
			}
			else
			{
				sendErrorMessage(socket, 400);
			}
		}
		// Handle GET method for HTTP
		else if (strcmp(request->method, "GET") == 0)
		{
			struct cache_element *temp = find(tempReq);
			if (temp != NULL)
			{
				int size = temp->len;
				int pos = 0;
				while (pos < size)
				{
					int chunk_size = (size - pos < MAX_BYTES) ? (size - pos) : MAX_BYTES;
					send(socket, temp->data + pos, chunk_size, 0);
					pos += chunk_size;
				}
				printf("Data retrieved from cache\n");
			}
			else
			{
				if (request->host && request->path && checkHTTPversion(request->version) == 1)
				{
					handle_request(socket, request, tempReq);
				}
				else
				{
					sendErrorMessage(socket, 400);
				}
			}
		}
		else
		{
			printf("Unsupported method: %s\n", request->method);
			sendErrorMessage(socket, 501);
		}

		ParsedRequest_destroy(request);
		free(tempReq);
	}

	free(buffer);
	shutdown(socket, SD_BOTH);
	closesocket(socket);
	ReleaseSemaphore(semaphore, 1, NULL);
	return 0;
}

int main(int argc, char *argv[])
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("WSAStartup failed: %d\n", WSAGetLastError());
		return 1;
	}

	SOCKET client_socketId;
	int client_len;
	struct sockaddr_in server_addr, client_addr;

	semaphore = CreateSemaphore(NULL, MAX_CLIENTS, MAX_CLIENTS, NULL);
	if (semaphore == NULL)
	{
		printf("CreateSemaphore failed: %d\n", GetLastError());
		WSACleanup();
		return 1;
	}

	InitializeCriticalSection(&cache_lock);

	if (argc == 2)
	{
		port_number = atoi(argv[1]);
	}
	else
	{
		printf("Too few arguments\n");
		WSACleanup();
		return 1;
	}

	printf("Setting Proxy Server Port : %d\n", port_number);

	proxy_socketId = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (proxy_socketId == INVALID_SOCKET)
	{
		printf("Failed to create socket: %d\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	int reuse = 1;
	if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) == SOCKET_ERROR)
	{
		printf("setsockopt failed: %d\n", WSAGetLastError());
	}

	ZeroMemory(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_number);
	server_addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
	{
		printf("Port is not free: %d\n", WSAGetLastError());
		closesocket(proxy_socketId);
		WSACleanup();
		return 1;
	}

	printf("Binding on port: %d\n", port_number);

	if (listen(proxy_socketId, MAX_CLIENTS) == SOCKET_ERROR)
	{
		printf("Error while Listening: %d\n", WSAGetLastError());
		closesocket(proxy_socketId);
		WSACleanup();
		return 1;
	}

	int i = 0;
	SOCKET Connected_socketId[MAX_CLIENTS];

	while (1)
	{
		ZeroMemory(&client_addr, sizeof(client_addr));
		client_len = sizeof(client_addr);

		client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, &client_len);
		if (client_socketId == INVALID_SOCKET)
		{
			printf("Error in Accepting connection: %d\n", WSAGetLastError());
			continue;
		}

		Connected_socketId[i] = client_socketId;

		struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr;
		char *ip_str = inet_ntoa(client_addr.sin_addr);
		printf("Client is connected with port number: %d and ip address: %s\n", ntohs(client_addr.sin_port), ip_str);

		thread_handles[i] = CreateThread(NULL, 0, thread_fn, &Connected_socketId[i], 0, NULL);
		if (thread_handles[i] == NULL)
		{
			printf("CreateThread failed: %d\n", GetLastError());
			closesocket(client_socketId);
			continue;
		}

		i = (i + 1) % MAX_CLIENTS;
	}

	closesocket(proxy_socketId);
	DeleteCriticalSection(&cache_lock);
	CloseHandle(semaphore);
	WSACleanup();
	return 0;
}

cache_element *find(char *url)
{
	EnterCriticalSection(&cache_lock);

	cache_element *site = NULL;
	if (head != NULL)
	{
		site = head;
		while (site != NULL)
		{
			if (!strcmp(site->url, url))
			{
				site->lru_time_track = time(NULL);
				break;
			}
			site = site->next;
		}
	}

	LeaveCriticalSection(&cache_lock);
	return site;
}

void remove_cache_element()
{
	if (head == NULL)
	{
		return;
	}

	cache_element *p = head;
	cache_element *q = head;
	cache_element *temp = head;

	for (q = head; q->next != NULL; q = q->next)
	{
		if ((q->next)->lru_time_track < temp->lru_time_track)
		{
			temp = q->next;
			p = q;
		}
	}

	if (temp == head)
	{
		head = head->next;
	}
	else
	{
		p->next = temp->next;
	}

	cache_size -= (temp->len) + sizeof(cache_element) + strlen(temp->url) + 1;
	free(temp->data);
	free(temp->url);
	free(temp);
}

int add_cache_element(char *data, int size, char *url)
{
	EnterCriticalSection(&cache_lock);

	int element_size = size + 1 + strlen(url) + sizeof(cache_element);
	if (element_size > MAX_ELEMENT_SIZE)
	{
		LeaveCriticalSection(&cache_lock);
		return 0;
	}

	while (cache_size + element_size > MAX_SIZE)
	{
		remove_cache_element();
	}

	cache_element *element = (cache_element *)malloc(sizeof(cache_element));
	if (!element)
	{
		LeaveCriticalSection(&cache_lock);
		return 0;
	}

	element->data = (char *)malloc(size + 1);
	if (!element->data)
	{
		free(element);
		LeaveCriticalSection(&cache_lock);
		return 0;
	}

	element->url = (char *)malloc(strlen(url) + 1);
	if (!element->url)
	{
		free(element->data);
		free(element);
		LeaveCriticalSection(&cache_lock);
		return 0;
	}

	strncpy(element->data, data, size);
	element->data[size] = '\0';
	strncpy(element->url, url, strlen(url));
	element->url[strlen(url)] = '\0';
	element->lru_time_track = time(NULL);
	element->next = head;
	element->len = size;
	head = element;
	cache_size += element_size;

	LeaveCriticalSection(&cache_lock);
	return 1;
}
