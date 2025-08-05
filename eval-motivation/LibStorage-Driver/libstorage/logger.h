#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

typedef enum {
	LOG_LEVEL_DEBUG = 0,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARN,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_NONE
} LogLevel;

#define STRINGIFY(x)	#x
#define TO_STRING(x)	STRINGIFY(x)
#define RELATIVE_PATH() "libstorage/" __FILE__

#define LOG_COLOR_DEBUG "\x1B[34m"
#define LOG_COLOR_INFO	"\x1B[32m"
#define LOG_COLOR_WARN	"\x1B[33m"
#define LOG_COLOR_ERROR "\x1B[31m"
#define LOG_COLOR_RESET "\x1B[0m"

#ifndef CURRENT_LOG_LEVEL
#define CURRENT_LOG_LEVEL LOG_LEVEL_INFO
#endif

#ifdef DEBUG
#define LOG(level, format, ...)                                               \
	do {                                                                  \
		if (level >= CURRENT_LOG_LEVEL) {                             \
			const char *color = LOG_COLOR_RESET;                  \
			const char *level_str = "";                           \
			switch (level) {                                      \
			case LOG_LEVEL_DEBUG:                                 \
				color = LOG_COLOR_DEBUG;                      \
				level_str = "DEBUG";                          \
				break;                                        \
			case LOG_LEVEL_INFO:                                  \
				color = LOG_COLOR_INFO;                       \
				level_str = "INFO";                           \
				break;                                        \
			case LOG_LEVEL_WARN:                                  \
				color = LOG_COLOR_WARN;                       \
				level_str = "WARN";                           \
				break;                                        \
			case LOG_LEVEL_ERROR:                                 \
				color = LOG_COLOR_ERROR;                      \
				level_str = "ERROR";                          \
				break;                                        \
			default:                                              \
				break;                                        \
			}                                                     \
			printf("[LibDriver]%s%d[%s] [%s:%d]: " format "%s\n", \
			       color, gettid(), level_str, RELATIVE_PATH(),   \
			       __LINE__, ##__VA_ARGS__, LOG_COLOR_RESET);     \
		}                                                             \
	} while (0)
#define ERR_DEBUG(format, ...)                                             \
	do {                                                               \
		if (CURRENT_LOG_LEVEL == LOG_LEVEL_DEBUG) {                \
			fprintf(stderr,                                    \
				"[LibDriver][STDERR] [%s:%d]" format "\n", \
				RELATIVE_PATH(), __LINE__, ##__VA_ARGS__); \
		}                                                          \
	} while (0)
#else
#define LOG(level, format, ...)
#define ERR_DEBUG(format, ...)
#endif

#define LOG_DEBUG(format, ...) LOG(LOG_LEVEL_DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  LOG(LOG_LEVEL_INFO, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  LOG(LOG_LEVEL_WARN, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LOG(LOG_LEVEL_ERROR, format, ##__VA_ARGS__)

#endif // LOGGER_H