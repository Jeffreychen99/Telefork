CC ?= cc
CFLAGS ?= -O0 -g -Wall -Wextra -Werror -std=c11

.PHONY: all test clean
all: build/counter

build/counter: examples/counter.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@

test: all
	python3 -m unittest discover -s tests -v

clean:
	rm -rf build
