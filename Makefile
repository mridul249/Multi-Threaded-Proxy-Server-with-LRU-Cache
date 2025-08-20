CC=gcc
CFLAGS= -g -Wall -std=c99
LDFLAGS= -lws2_32

all: proxy_server

proxy_server: proxy_server.c proxy_parse.c proxy_parse.h
	$(CC) $(CFLAGS) -o proxy_parse.o -c proxy_parse.c
	$(CC) $(CFLAGS) -o proxy_server.o -c proxy_server.c
	$(CC) $(CFLAGS) -o proxy_server proxy_parse.o proxy_server.o $(LDFLAGS)

clean:
	rm -f proxy_server *.o

tar:
	tar -cvzf proxy_server.tgz proxy_server.c README.md Makefile proxy_parse.c proxy_parse.h
