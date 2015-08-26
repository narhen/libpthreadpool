CC = gcc
CFLAGS = -Wall -O2 -I ../includes
LDFLAGS = -lpthread

EXAMPLES = 

%.o: %.c
	$(CC) $< $(CFLAGS) -c -o $@

all: $(EXAMPLES)

clean:
	-$(RM) *.o
	-$(RM) $(EXAMPLES)
