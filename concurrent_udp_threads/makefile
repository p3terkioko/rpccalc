all: client server

client: client.c
	gcc -Wall -o client client.c

server: server.c
	gcc -Wall -pthread -o server server.c

clean:
	rm -f client server *.o server.log
