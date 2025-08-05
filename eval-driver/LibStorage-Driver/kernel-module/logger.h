#ifndef LOGGER_H
#define LOGGER_H

#include <linux/kernel.h>

typedef enum {
	LOG_LEVEL_DEBUG = 0,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARN,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_NONE
} LogLevel;

#ifndef CURRENT_LOG_LEVEL
#define CURRENT_LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#define KERNEL_LOG_LEVEL(level)                    \
	(level == LOG_LEVEL_DEBUG ? KERN_DEBUG :   \
	 level == LOG_LEVEL_INFO  ? KERN_INFO :    \
	 level == LOG_LEVEL_WARN  ? KERN_WARNING : \
	 level == LOG_LEVEL_ERROR ? KERN_ERR :     \
				    KERN_INFO)

#ifdef DEBUG
#define STRINGIFY(x)	#x
#define TO_STRING(x)	STRINGIFY(x)
#define RELATIVE_PATH() (&__FILE__[sizeof(TO_STRING(PROJ_DIR)) - 1])
#define LOG(level, format, ...)                                          \
	do {                                                             \
		if (level >= CURRENT_LOG_LEVEL) {                        \
			printk("%s[%s:%d] [%s]: " format "\n",           \
			       KERNEL_LOG_LEVEL(level), RELATIVE_PATH(), \
			       __LINE__, __func__, ##__VA_ARGS__);       \
		}                                                        \
	} while (0)
#else
#define LOG(level, format, ...)
#endif // DEBUG

#define LOG_DEBUG(format, ...) LOG(LOG_LEVEL_DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  LOG(LOG_LEVEL_INFO, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  LOG(LOG_LEVEL_WARN, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LOG(LOG_LEVEL_ERROR, format, ##__VA_ARGS__)

#endif // KERNEL_LOGGER_H