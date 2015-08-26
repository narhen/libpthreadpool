CC = gcc
CFLAGS = -Wall -O2 -I ../includes
LDFLAGS = -lpthread

TESTS = increment

%.o: %.c
	$(CC) $< $(CFLAGS) -c -o $@

all: $(TESTS)

increment: increment.o ../pthreadpool.o
	$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

clean:
	-$(RM) *.o
	-$(RM) $(TESTS)
