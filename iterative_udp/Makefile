# Makefile for UDP Calculator Project

CC = gcc
CFLAGS = -Wall -g

# Executable names
SERVER = server
CLIENT = client

# Source files
SERVER_SRC = server.c
CLIENT_SRC = client.c

.PHONY: all clean

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC)

$(CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC)

clean:
	rm -f $(SERVER) $(CLIENT)
