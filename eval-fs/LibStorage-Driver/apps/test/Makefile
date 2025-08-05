CC = gcc
CFLAGS = -Wall -I../../include -O0 -g
LDFLAGS = -L../../lib -lstorage

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)
TARGET = test_app

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)