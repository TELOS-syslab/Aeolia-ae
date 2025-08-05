# build the fio plugin library
FIO_DIR := ../../../fio
CC = gcc
CFLAGS = -I../../include -I$(FIO_DIR) -fPIC -D_GNU_SOURCE -muintr -g  -mgeneral-regs-only -mwaitpkg
LDFLAGS = -L../../lib -lstorage -shared 
TARGET = ls_fio

OBJS = $(addsuffix .o, $(TARGET))
default: all ls_fio
all: $(OBJS) $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(LOGLEVEL) $(OPTIMIZE) -c $< -o $@
ls_fio:
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)