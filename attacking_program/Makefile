CC     = cc
CFLAGS = -Wall -Wextra -O2
PORT  ?= 4444

.PHONY: all clean run rebuild

all: c2

c2: c2.c
	$(CC) $(CFLAGS) -o $@ $<

run: c2
	./c2 $(PORT)

rebuild: clean all

clean:
	rm -f c2
