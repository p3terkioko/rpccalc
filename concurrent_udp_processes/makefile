# Makefile for UDP Calculator Client and Server

CC = gcc
CFLAGS = -Wall -Wextra -O2

# Targets
all: client server

client: client.c
	$(CC) $(CFLAGS) -o client client.c

server: server.c
	$(CC) $(CFLAGS) -o server server.c

# Clean up
clean:
	rm -f client server *.o

# Phony targets
.PHONY: all clean
