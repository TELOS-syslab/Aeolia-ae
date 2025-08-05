CFLAGS = -g -O0 -pthread -Iinclude/ -Wunused -DMPK_PROTECTION
LDFLAGS = -pthread -ldl -lnuma -ltcmalloc_minimal 

TARGET = main
OBJS = main.o trampoline.o mpk.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	gcc $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	gcc -c $< -o $@ $(CFLAGS)

%.o: %.S
	gcc -c $< -o $@  

clean:
	rm -rf $(TARGET) $(OBJS)