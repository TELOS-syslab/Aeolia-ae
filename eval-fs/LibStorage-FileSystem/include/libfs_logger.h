#ifndef _LOGGER_H_
#define _LOGGER_H_
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#define  gettid() syscall(SYS_gettid)
#define  RELATIVE_PATH() "LibStorage-FileSystem/libfs/" __FILE__
#define  LOG_COLOR_RESET "\x1B[0m"
#define LOG_COLOR_DEBUG "\x1B[33m"
#define LEVEL_STR "DEBUG"

#ifdef DEBUG
#define LOG_FS(fmt, ...)                                                       \
    printf("\033[0;34m[LibFS]" fmt "\033[0m", ##__VA_ARGS__)
#else
#define LOG_FS(fmt, ...) // Empty
#endif

#define WARN_FS(fmt, ...)                                                      \
    printf("\033[0;33m[LibFS]" fmt "\033[0m", ##__VA_ARGS__)
#endif

#define DEBUG(format, ...)                                         \
do {                                                                       \
        printf("[LibFs]%s%d[%s] [%s:%d]: " format "%s\n", LOG_COLOR_DEBUG,      \
               gettid(), LEVEL_STR, RELATIVE_PATH(), __LINE__,             \
               ##__VA_ARGS__, LOG_COLOR_RESET);                            \
} while (0)