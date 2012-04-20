CC=gcc
CFLAGS = -g -Wall
LDFLAGS = -llo -lm

all: thrum

clean:
	rm -f *.o *~ thrum

thrum: thrum.o Makefile
	$(CC) thrum.o  $(CFLAGS) $(LDFLAGS) -o thrum
