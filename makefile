all: server.c
	gcc server.c threadpool.c -lpthread -Wall -o server
all-GDB: server.c
	gcc -g server.c threadpool.c -lpthread -Wall -o server
