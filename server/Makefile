TARGET = aesdsocket
CC := $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror -g

.PHONY: all clean

default: all
all: clean aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c

clean:
	rm -f aesdsocket