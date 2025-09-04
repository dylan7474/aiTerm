# File: Makefile.linux
CC = gcc
TARGET = ai-shell
SRCS = main.c
CFLAGS = -Wall -O2
LDFLAGS = -lcurl -ljansson

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
