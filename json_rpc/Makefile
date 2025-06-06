CC = gcc
CFLAGS = -Wall -g -I/usr/include/json-c
LDFLAGS = -ljson-c

TARGET_SERVER = server
TARGET_CLIENT = client
SRC_SERVER = server.c
SRC_CLIENT = client.c

all: $(TARGET_SERVER) $(TARGET_CLIENT)

$(TARGET_SERVER): $(SRC_SERVER)
	$(CC) $(CFLAGS) -o $(TARGET_SERVER) $(SRC_SERVER)

$(TARGET_CLIENT): $(SRC_CLIENT)
	$(CC) $(CFLAGS) -o $(TARGET_CLIENT) $(SRC_CLIENT) $(LDFLAGS)

clean:
	rm -f $(TARGET_SERVER) $(TARGET_CLIENT) *.o

.PHONY: all clean
