.PHONY: llist.o centralizedChatServer.o centralizedChatServer

# Set compiler to use
CC=gcc
CFLAGS=
DEBUG=0

ifeq ($(DEBUG),1)
	CFLAGS+=-g -O0
else
	CFLAGS+=-O2
endif

centralizedChatServer: llist.o centralizedChatServer.o
	$(CC) $(CFLAGS) -o centralizedChatServer llist.o centralizedChatServer.o -lpthread

centralizedChatServer.o: llist.o
	$(CC) $(CFLAGS) -c centralizedChatServer.c -o centralizedChatServer.o

llist.o: 
	$(CC) $(CFLAGS) -c llist2.c -o llist.o
clean: 
	rm -f centralizedChatServer
	rm -f *.o
	rm -f *~
