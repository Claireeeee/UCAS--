alla: echo-client echo-server
all: client server

client: client.c
	gcc -Wall -g client.c -o client

server: server.c
	gcc -Wall -g server.c -o server -lpthread

clean:
	@rm -f client server


echo-client: echo-client.c
	gcc -Wall -g echo-client.c -o echo-client

echo-server: echo-server.c
	gcc -Wall -g echo-server.c -o echo-server

cleana:
	@rm -f echo-client echo-server
