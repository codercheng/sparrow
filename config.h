#ifndef _CONFIG_H
#define _CONFIG_H

#ifdef __cplusplus

extern "C" 
{
#endif

#define CONFIG_FILE_PATH "config/sparrow.conf"

/**
 * default configuration value
 */
#define DEF_LISTEN_PORT 6868
#define DEF_MAX_EVENT 1024
#define DEF_USE_EPOLL_ET 1
#define DEF_USE_TCP_CORK 1
#define DEF_ROOT_DIR "./www/"

#define DEF_CACHE_CONTROL_MAX_AGE 300

#define DEF_LOG_TIME_OUT 60
#define DEF_LOG_LEVEL 2
#define DEF_LOG_ENABLE 1

#define DEF_WORKER_THREAD_NUM 3
#define DEF_HOME_PAGE "index.html"

#define DEF_MAX_SUB_ITEM_NUM 100

#define IS_EQUAL(str1, str2) strncmp(str1, str2, strlen(str2))== 0

typedef struct {
	int listen_port;
	int max_conn;
	int use_epoll_et;
	int use_tcp_cork;

	char root_dir[512];

	int cache_control_max_age;

	//log
	int log_enable;
	int log_time_out;
	int log_level;

	int worker_thread_num;

	char def_home_page[128];
	
	//folder
	int max_sub_item_num;

} config_t;

extern config_t conf;
extern int read_config(config_t *conf);

#ifdef __cplusplus
}
#endif


#endif
