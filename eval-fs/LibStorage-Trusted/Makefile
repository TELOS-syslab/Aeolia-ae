# https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html

#ASAN_OPTIONS=verify_asan_link_order=0
#CFLAGS = -O0 -g -pthread -Iinclude/ -Wunused -fsanitize=address
#CFLAGS = -O0 -g -pthread -Iinclude/ -Wunused

#LDFLAGS = -shared -pthread -O0 -g -ldl -lnuma -fsanitize=address
#LDFLAGS = -shared -pthread -O0 -g -ldl -lnuma

CFLAGS = -Ofast -pthread -Iinclude/ -Wunused -ltcmalloc_minimal -g #-DMPK_PROTECTION

LDFLAGS = -shared -pthread -Ofast -ldl -lnuma -ltcmalloc_minimal -lstorage 

TARGET= libtrusted.so

DEBUG ?= 0
OPTIMIZE = -O2
ifeq ($(DEBUG), 1)
    LOGLEVEL += -DDEBUG -DCURRENT_LOG_LEVEL=LOG_LEVEL_DEBUG
else
	LOGLEVEL += -DDEBUG -DCURRENT_LOG_LEVEL=LOG_LEVEL_WARN
endif

OBJS = trampoline.o \
	   tls.o \
	   trusted-fs.o \
	   balloc.o \
	   ialloc.o \
	   cmd.o \
	   journal.o \
	   super.o \
	   fsop.o \
	   mpk.o \
	   bravo.o \
	   rbtree.o \
	   rb.o \
	   rwlock_bravo.o \
	   radix_array.o \
	   chainhash.o \

.PHONY: default
default: all

.PHONY: all
all: lib install

.PHONY: lib
lib: $(TARGET)

$(TARGET): $(OBJS)
	gcc  $^ -o $@ $(CFLAGS) $(LDFLAGS) $(LOGLEVEL)
	
%.o: %.c
	gcc -fPIC -c $(CFLAGS) $(LOGLEVEL) $^

%.o: %.S
	gcc -fPIC -c $(CFLAGS) $(LOGLEVEL) $^

.PHONY: install
install:
	sudo cp $(TARGET) /usr/lib/
	
.PHONY: clean
clean:
	rm -rf *.o *.so