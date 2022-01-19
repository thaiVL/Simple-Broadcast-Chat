CC = gcc
CFLAGS = -Wall -g

.PHONY: all clean

all: server recv send

server: server.c
	$(CC) server.c -Wall -o server -lpthread

recv: receiver.c
	$(CC) receiver.c -Wall -o recv -lpthread

send: sender.c
	$(CC) sender.c -Wall -o send -lpthread


# server: server.o
# 	$(CC) -o server server.o -lpthread

# server.o:
# 	$(CC) -c server.c -lpthread

# sender: sender.o
# 	$(CC) -o sender sender.o -lpthread

# sender.o:
# 	$(CC) -c sender.c -lpthread

# recv: recv.o
# 	$(CC) -o recv recv.o -lpthread 

# recv.o:
# 	$(CC) -c receiver.c -lpthread