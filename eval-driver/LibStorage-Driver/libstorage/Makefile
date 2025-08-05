
CC = gcc

CFLAGS = -pthread -I$(CURDIR)/../include/ -I$(CURDIR) -g
CFLAGS += -muintr -mgeneral-regs-only -lbpf

LDFLAGS = -shared -pthread -ldl -lnuma -lbpf

TARGET = libstorage.so

OBJS = libstorage.o dma_pool.o io.o sched.o

$(info Current directory: $(CURDIR) $(EXTRA_CFLAGS) $(PROJ_DIR))

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) $(CFLAGS) $(LOGLEVEL)  $(OPTIMIZE) 

%.o: %.c
	$(CC) -c -o $@ $^ $(CFLAGS) $(LOGLEVEL) -fPIC 

.PHONY: clean
clean:
	rm -rf *.o *.so
	sudo rm -f /usr/lib/$(TARGET)

.PHONY: install
install:
	sudo cp $(TARGET) /usr/lib/