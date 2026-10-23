CC ?= clang
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?= 

TARGET = eifisier
SRCS = main.c scanner.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c scanner.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
