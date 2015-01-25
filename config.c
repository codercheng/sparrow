#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "config.h"

//////////////////////////////////////////////////
/*initialize the conf object*/
config_t conf = {
	DEF_LISTEN_PORT,
	DEF_MAX_EVENT,
	DEF_USE_EPOLL_ET,
	DEF_USE_TCP_CORK,
	DEF_ROOT_DIR,
	DEF_CACHE_CONTROL_MAX_AGE,
	DEF_LOG_ENABLE,
	DEF_LOG_TIME_OUT,
	DEF_LOG_LEVEL,
	DEF_WORKER_THREAD_NUM,
	DEF_HOME_PAGE,
	DEF_MAX_SUB_ITEM_NUM
};
/////////////////////////////////////////////////

/**
 * remove the space character at the left of the str
 */
static
char * trim_left(char *str) {
	char *p;
	if(str == NULL)
		return NULL;
	p = str;
	while(*p == ' ' && *p != '\0') {
		++p;
	}
	return memmove(str, p, strlen(p)+1);
}

/**
 * remove the space character at the right of the str
 */
static
char * trim_right(char *str) {
	int len;
	if(str == NULL)
		return NULL;

	len = strlen(str);
	while(len) {
		if(*(str+len-1) == ' ') {
			*(str+len-1) = '\0';
		} else {
			break;
		}
		len--;
	}
	return str;
}

/**
 * get the key and value of the line
 */
static
int get_kv(char *line, char **key, char **value) {
	char *p;
	char *t;
	trim_left(line);
	p = strchr(line, '=');
	if(p == NULL) {
		fprintf(stderr, "BAD FORMAT:%s\n", line);
		return -1;
	}
	*key = line;
	*value = p+1;
	*p = '\0';
	trim_right(*key);
	

	//remove the '\n' at the end of the line
	t = strrchr(*value, '\n');
	if(t != NULL)
		*t = '\0';
	t = strrchr(*value, '\r');
	if(t!= NULL)
		*t = '\0';
	trim_left(*value);
	trim_right(*value);
	return 0;
}

/**
 * read the configuration file, and export to conf object
 */
int read_config(config_t *conf) {
	char config_line[512];
	FILE *fp = fopen(CONFIG_FILE_PATH, "r");
	if(fp == NULL) {
		fprintf(stderr, "Can not open config file:%s\n", strerror(errno));
		return -1;
	}
	
	while(!feof(fp)) {
		int ret;
		char *key = NULL;
		char *value = NULL;
		memset(config_line, 0, sizeof(config_line));
		fgets(config_line, 512, fp);
		
		trim_left(config_line);
		if(config_line[0] == '#' || config_line[0] == '\n'|| config_line[0] == '\r' || strcmp(config_line, "") == 0)
			continue;
		
		ret = get_kv(config_line, &key, &value);
		if(ret != -1) {
			if(IS_EQUAL(key, "listen_port")) {
				conf->listen_port = atoi(value);
			} else if(IS_EQUAL(key, "max_conn")) {
				conf->max_conn = atoi(value);
			} else if(IS_EQUAL(key, "use_epoll_et")) {
				conf->use_epoll_et = atoi(value);
			} else if(IS_EQUAL(key, "use_tcp_cork")) {
				conf->use_tcp_cork = atoi(value);
			} else if(IS_EQUAL(key, "root_dir")) {
				if(strcmp(value, "") == 0)
					continue;
				strcpy(conf->root_dir, value);
				int len = strlen(conf->root_dir);
				//路径不需要加 '/'
				if(len != 1) {
					if(conf->root_dir[len-1] == '/') {
						conf->root_dir[len-1] = '\0';						
					}
				}
				// if(conf->root_dir[len-1] != '/') {
				// 	conf->root_dir[len] = '/';
				// 	conf->root_dir[len+1] = '\0';
				// }
				
			} else if(IS_EQUAL(key, "cache_control_max_age")) {
				conf->cache_control_max_age = atoi(value);
			} else if(IS_EQUAL(key, "log_time_out")) {
				conf->log_time_out = atoi(value);
			} else if(IS_EQUAL(key, "log_level")) {
				conf->log_level = atoi(value);
			} else if(IS_EQUAL(key, "worker_thread_num")) {
				conf->worker_thread_num = atoi(value);
			} else if(IS_EQUAL(key, "log_enable")) {
				conf->log_enable = atoi(value);
			} else if(IS_EQUAL(key, "default_home_page")) {
				strcpy(conf->def_home_page, value);
			} else if(IS_EQUAL(key, "max_sub_item_num"))  {
				conf->max_sub_item_num = atoi(value);
			} else {
				continue;
			}
		}
		
	}
	fclose(fp);
	return 0;
}
