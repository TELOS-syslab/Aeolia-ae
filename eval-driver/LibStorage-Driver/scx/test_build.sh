meson compile -C build scx_eevdf
gcc -O2 -Wall sched_test.c -o sched_test -lbpf
