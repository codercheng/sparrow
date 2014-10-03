#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/signal.h>
#include <assert.h>

#include "async_log.h"


int g_time_out_sec;
int g_min_log_level;


static log_buf_t *w_buf;
static log_buf_t *r_buf;

static int log_fd;
static pthread_t log_pid;
static uint32_t time_zone;
static char log_path[MAX_PATH];

static int cur_log_year;
static int cur_log_mon;
static int cur_log_day;
static int cur_log_day_num;
static int g_new_fd = 0;

static pthread_mutex_t mutex;
static pthread_cond_t cond;


static void log_write_impl(const char *file, int line, const char *log_level_str, const char *format, va_list ap);
static void write_to_file_inner(int fd, const char* buf, int size);
static time_info_t get_cur_time(void);
static int create_new_log(void);
static void *write_to_file(void *arg);
static void safe_exit(int sig);


void set_time_out_sec(int second) {
	g_time_out_sec = second;
}
void set_min_log_level(int log_level) {
	g_min_log_level = log_level;
}


void log_write(const char *file, int line, uint8_t log_level, const char *format, ...) {
	va_list ap;
	va_start(ap, format);

	if(log_level >= g_min_log_level) {
		const char *log_level_str;
		switch(log_level) {
			case LOG_TRACE:
				log_level_str = LOG_TRACE_STR;
				break;
			case LOG_DEBUG:
	            log_level_str = LOG_DEBUG_STR;
	            break;
	        case LOG_INFO:
	            log_level_str = LOG_INFO_STR;
	            break;
	        case LOG_WARN:
	            log_level_str = LOG_WARN_STR;
	            break;
	        case LOG_ERROR:
	            log_level_str = LOG_ERROR_STR;
	            break;
	        case LOG_FATAL:
	            log_level_str = LOG_FATAL_STR;
	            break;
	         default:
	         	break;
		}
		log_write_impl(file, line, log_level_str, format, ap);
	}
	va_end(ap);
}


static 
void log_write_impl(const char *file, int line, const char *log_level_str, const char *format, va_list ap) {
	char buf_text[1024];
	char buf_time[32];
	memset(buf_text, 0, sizeof(buf_text));
	memset(buf_time, 0, sizeof(buf_time));
	int count_text;
	int count_time;

	/**
	* thus, no need to call a system call gettid() everytime
	*/
	static __thread int t_tid = -1;

	if(t_tid == -1) {
		t_tid = gettid();
	}
	
	count_text = sprintf(buf_text, " %-6s %d %s:%d ", log_level_str, t_tid, file, line);
	count_text += vsprintf(buf_text + count_text, format, ap);
	if(buf_text[count_text-1] != '\n') {
		buf_text[count_text] = '\n';
		buf_text[++count_text] = '\0';
	} else {
		buf_text[count_text] = '\0';
	}

	time_info_t ti;
	
	while(1) {
		pthread_mutex_lock(&mutex);

		/****************************************************************/
		/**
		 * 这个地方可以优化一下
		 * 当第一次失败后，返回来在写日志的时候，又重新写了一遍时间
		 * 是否合理，还需要在仔细琢磨一下
		 */
		ti = get_cur_time();

		count_time = sprintf(buf_time, "[ %02d:%02d:%02d.%06ld ]", ti.hour, ti.min, ti.sec, ti.usec);

		/****************************************************************/

		/**
		* create a new log file
		*/
		if(ti.day_num > cur_log_day_num) {
			g_new_fd = 1;
			pthread_cond_signal(&cond);
			pthread_mutex_unlock(&mutex);
		}
		/**
		* buf is full
		*/
		if(w_buf->pos + count_time + count_text >= MAX_BUF_SIZE) {
			pthread_cond_signal(&cond);
			pthread_mutex_unlock(&mutex);
		} else {
			strncpy(w_buf->buf+w_buf->pos, buf_time, count_time);
			w_buf->pos += count_time;
			strncpy(w_buf->buf+w_buf->pos, buf_text, count_text);
			w_buf->pos += count_text;
			
			pthread_mutex_unlock(&mutex);
			break;
		}
	}
}


static 
void write_to_file_inner(int fd, const char* buf, int size)
{
    int ret = -1;
    do
    {
        ret = write(fd, buf, size);
    } while((ret < 0) && (errno == EINTR));
}

static
void *write_to_file(void *arg) {
	int ret;
	struct timespec ts;
	int new_fd = -1;

	while(1) {
		pthread_mutex_lock(&mutex);

		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += g_time_out_sec;
		if(r_buf->pos == 0) {
			ret = pthread_cond_timedwait(&cond, &mutex, &ts);
			// if(ret == ETIMEDOUT) {
			// 	debug_printf("time out\n");
			// } else if(ret == 0) {
			// 	debug_printf("signaled\n");
			// } else {
			// 	debug_printf("error in pthread_cond_timedwait\n");
			// }
			if(ret == -1) {
				fprintf(stderr, "error in pthread_cond_timedwait\n");
			}
		}
		/**
		* swap the read buf and write buf
		*/
		log_buf_t *tmp = r_buf;
		r_buf = w_buf;
		w_buf = tmp;

		/**
		* if g_new_fd, create a new log file
		*/
		if(g_new_fd) {
			new_fd = create_new_log();
			g_new_fd = 0;
		}
		
		pthread_mutex_unlock(&mutex);
			
		if(r_buf->pos) {
			write_to_file_inner(log_fd, r_buf->buf, r_buf->pos);
			r_buf->pos = 0;
		}

		if(new_fd != -1) {
			int tmp_fd = log_fd;
			log_fd = new_fd;
			close(tmp_fd);
		}
	}//end while
	return NULL;
}

static 
int create_new_log(void) {
	char buf[MAX_PATH];
	int fd;

	time_t tt;
	struct tm ts;
	time(&tt);
	localtime_r(&tt, &ts);

	cur_log_year = ts.tm_year + 1900;
	cur_log_mon = ts.tm_mon + 1;
	cur_log_day = ts.tm_mday;
	//sprintf(buf,"log/");
	if(access("log", F_OK) != 0) {
		if(mkdir("log", 0755) < 0) {
			fprintf(stderr, "create log folder error\n");
		}
	}
	
	int count = sprintf(buf, "%s/log/%04d-%02d-%02d.log", log_path, cur_log_year, cur_log_mon, cur_log_day);
	buf[count] = '\0';
	fd = open(buf, O_WRONLY|O_CREAT|O_APPEND, 0644);
	if(fd == -1) {
		fprintf(stderr, "error create log fd\n");
	}
	return fd;
}

static 
time_info_t get_cur_time(void) {
	time_info_t ti;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	int t = tv.tv_sec%(60*60*24);
	ti.hour = t/(60*60) + time_zone;
	if(ti.hour >= 24) {
		ti.hour -= 24;
	}
	t %= (60*60);
	ti.min = t/60;
	ti.sec = t%60;
	ti.usec = tv.tv_usec;
	ti.day_num = (tv.tv_sec+time_zone*(60*60))/(60*60*24);
	return ti;
}

static 
void safe_exit(int sig) {
    log_destroy();
    exit(1);
}

void log_init(int second, int log_level) {
	int ret;
	struct timezone tz;
	struct timeval tv;
	time_info_t ti;

	/**
	*
	*/
	signal(SIGINT,  safe_exit);
    signal(SIGTERM, safe_exit);
    signal(SIGQUIT, safe_exit);

	set_time_out_sec(second);
	set_min_log_level(log_level);

	ret = gettimeofday(&tv, &tz);
	assert(ret == 0);
	time_zone = -tz.tz_minuteswest/60;

	int t = tv.tv_sec%(60*60*24);
	ti.hour = t/(60*60) + time_zone;
	if(ti.hour >= 24) {
		ti.hour -= 24;
	}
	t %= (60*60);
	ti.min = t/60;
	ti.sec = t%60;
	ti.usec = tv.tv_usec;

	cur_log_day_num = (tv.tv_sec+time_zone*(60*60))/(60*60*24);

	getcwd(log_path, MAX_PATH);
	log_fd = create_new_log();

	w_buf = (log_buf_t *)malloc(sizeof(log_buf_t));
	r_buf = (log_buf_t *)malloc(sizeof(log_buf_t));

	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&cond, NULL);

	char buf[256];
	int count = 0;

	ret = pthread_create(&log_pid, NULL, write_to_file, NULL);
	pthread_detach(log_pid);
	
	if(ret == -1) {
		count = sprintf(buf, "[ %02d:%02d:%02d.%06ld ] %-6s %ld %s:%d %s\n", ti.hour, ti.min, ti.sec, ti.usec,\
							 LOG_ERROR_STR, gettid(), __FILE__, __LINE__, "create async_log thread error");
		buf[count] = '\0';
		write_to_file_inner(log_fd, buf, count);
	} else {
		count = sprintf(buf, "[ %02d:%02d:%02d.%06ld ] %-6s %ld %s:%d %s\n", ti.hour, ti.min, ti.sec, ti.usec, \
						LOG_INFO_STR, gettid(), __FILE__, __LINE__, "create async_log thread successfully");
		buf[count] = '\0';
		write_to_file_inner(log_fd, buf, count);
	}
}
void log_destroy() {
	log_info("Log is going to be closed...");  
    /**
    * write the data, which haven't writen to the log file in the buf, to log file
    */
    write_to_file_inner(log_fd, w_buf->buf, w_buf->pos);
    
    pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
	free(w_buf);
	free(r_buf);
}