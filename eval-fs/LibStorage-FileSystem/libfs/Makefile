# https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html

#ASAN_OPTIONS=verify_asan_link_order=0
#CFLAGS = -O0 -g -pthread -Iinclude/ -Wunused -fsanitize=address
#CFLAGS = -O0 -g -pthread -Iinclude/ -Wunused
CFLAGS = -Ofast -pthread -Iinclude/ -Wunused -ltcmalloc_minimal -g

#LDFLAGS = -shared -pthread -O0 -g -ldl -lnuma -fsanitize=address
#LDFLAGS = -shared -pthread -O0 -g -ldl -lnuma
LDFLAGS = -shared -pthread -Ofast -ldl -lnuma -ltcmalloc_minimal -ltrusted

TARGET=sufs.so

OBJS = bravo.o \
       chainhash.o \
       cmd.o \
       dir.o \
       entry.o \
       filetable.o \
	file.o \
       mfs.o \
       mnode.o \
       proc.o \
       radix_array.o \
       rb.o \
       rbtree.o \
       rwlock_bravo.o \
       stat.o  \
       super.o \
       syscall.o \
       tls.o \
       util.o

.PHONY: default
default: $(TARGET)

$(TARGET): $(OBJS)
	gcc  $^ -o $@ $(CFLAGS) $(LDFLAGS) $(LOGFLAG)
	
%.o: %.c
	gcc -fPIC -c $(CFLAGS) $(LOGFLAG) $^

%.o: %.S
	gcc -fPIC -c $(CFLAGS) $(LOGFLAG) $^

.PHONY: install
install:
	sudo cp $(TARGET) /usr/lib/libsufs.so
	
.PHONY: clean
clean:
	rm -rf *.o *.so