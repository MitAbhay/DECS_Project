CC = gcc
CFLAGS = -O2 -Wall -pthread
LIBS = -lmicrohttpd -lpq -I/usr/include/postgresql

all: server loadgen

server: server.c skiplist.c skiplist.h config.h
	$(CC) $(CFLAGS) server.c -o server $(LIBS)

loadgen: loadgen.c
	$(CC) $(CFLAGS) loadgen.c -o loadgen -lcurl

clean:
	rm -f server loadgen
