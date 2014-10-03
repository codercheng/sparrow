#ifndef _LOG_H_
#define _LOG_H_

#include <stdarg.h>
#include <stdint.h>
#include <sys/syscall.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MAX_PATH 256
#define MAX_BUF_SIZE 1024 * 1024 * 4

#define LOG_TRACE_STR "TRACE"
#define LOG_DEBUG_STR "DEBUG"
#define LOG_INFO_STR  "INFO"
#define LOG_WARN_STR  "WARN"
#define LOG_ERROR_STR "ERROR"
#define LOG_FATAL_STR "FATAL"


#define log_trace(format, ...)    log_write(__FILE__, __LINE__, LOG_TRACE, format, ##__VA_ARGS__);
#define log_debug(format, ...)    log_write(__FILE__, __LINE__, LOG_DEBUG, format, ##__VA_ARGS__);
#define log_info(format, ...)     log_write(__FILE__, __LINE__, LOG_INFO, format, ##__VA_ARGS__);
#define log_warn(format, ...)     log_write(__FILE__, __LINE__, LOG_WARN, format, ##__VA_ARGS__);
#define log_error(format, ...)    log_write(__FILE__, __LINE__, LOG_ERROR, format, ##__VA_ARGS__);
#define log_fatal(format, ...)    log_write(__FILE__, __LINE__, LOG_FATAL, format, ##__VA_ARGS__);

#define gettid() syscall(__NR_gettid)

typedef struct log_buf {
	char buf[MAX_BUF_SIZE];
	int pos;
}log_buf_t;


enum {
	LOG_TRACE = 0,
	LOG_DEBUG = 1,
	LOG_INFO  = 2,
	LOG_WARN  = 3,
	LOG_ERROR = 4,
	LOG_FATAL = 5
};

typedef struct time_info {
	int day_num;
	int hour;
	int min;
	int sec;
	long int usec;
}time_info_t;


extern void log_write(const char *file, int line, uint8_t log_level, const char *format, ...);
extern void set_time_out_sec(int second);
extern void set_min_log_level(int log_level);
extern void log_init(int second, int log_level);
extern void log_destroy();


#ifdef __cplusplus
}
#endif

#endif// _LOG_H_