CC ?= gcc
CFLAGS ?= -O2 -Wall -Iapp/src/main/cpp

all: test

test: tests/test_parsers.c app/src/main/cpp/bench.c app/src/main/cpp/sys_cpu.c app/src/main/cpp/sys_mem.c
	$(CC) $(CFLAGS) tests/test_parsers.c app/src/main/cpp/bench.c app/src/main/cpp/sys_cpu.c app/src/main/cpp/sys_mem.c -lm -o run_tests
	./run_tests
	rm -f run_tests

clean:
	rm -f run_tests *.o *.so app-debug.apk base.apk

.PHONY: all test clean
