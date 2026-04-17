CC = gcc
CFLAGS = -Iinclude
LIBS = -lpthread

all:
	$(CC) src/*.c $(CFLAGS) -o game $(LIBS)

clean:
	rm -f game