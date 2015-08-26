CC = gcc
CFLAGS = -Wall -O2 -fPIC -I includes -g
LDFLAGS = -shared -lpthread

all: libpthreadpool

%.o: %.c
	$(CC) $< $(CFLAGS) -c -o $@

libpthreadpool: pthreadpool.o
	$(CC) $^ $(LDFLAGS) -o $@.so

.PHONY: tests
tests:
	$(MAKE) -C tests all

clean:
	-$(RM) *.o
	-$(RM) *.so
	$(MAKE) -C tests clean
