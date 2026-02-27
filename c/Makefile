CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic -O2
LDFLAGS = -lm

.PHONY: all clean test

all: corvus_demo

corvus_demo: corvus_demo.c corvus_bms.c corvus_bms.h
	$(CC) $(CFLAGS) -o $@ corvus_demo.c corvus_bms.c $(LDFLAGS)

test_corvus: test_corvus.c corvus_bms.c corvus_bms.h
	$(CC) $(CFLAGS) -o $@ test_corvus.c corvus_bms.c $(LDFLAGS)

test: test_corvus
	./test_corvus

debug: CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -g
debug: LDFLAGS = -lm -fsanitize=address,undefined
debug: corvus_demo test_corvus

clean:
	rm -f corvus_demo test_corvus corvus_output.csv
