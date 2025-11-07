CC = gcc
CFLAGS = -O2 -Wall -pthread
INCLUDES = -I/usr/include/postgresql
LIBS_SERVER = -lmicrohttpd -lpq
LIBS_LOADGEN = -lcurl -lpthread

# Executable names
SERVER = server
LOADGEN = loadgen

# Source files
SERVER_SRC = server.c
LOADGEN_SRC = loadgen.c

# Default target
all: $(SERVER) $(LOADGEN)

$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(SERVER) $(SERVER_SRC) $(LIBS_SERVER)

$(LOADGEN): $(LOADGEN_SRC)
	$(CC) $(CFLAGS) -o $(LOADGEN) $(LOADGEN_SRC) $(LIBS_LOADGEN)

clean:
	rm -f $(SERVER) $(LOADGEN)