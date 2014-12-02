#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

#include "file.h"
#include "global.h"
#include "config.h"
#include "async_log.h"



static
int items_cmp(const void *a, const void *b) {
	item_t *p1 = (item_t *)a;
	item_t *p2 = (item_t *)b;
	if(p1->dir != p2->dir) {
		return (p1->dir > p2->dir)?-1:1;
	} else {
		return (p1->m_time > p2->m_time)?-1:1;
	}
}

int isItFolder(const char *path){
	struct stat s;
	if(0 == stat(path, &s)){
		if(S_ISDIR(s.st_mode))
			return 1;
	}
	return 0;
}

int isItFile(const char *path){
	struct stat s;
	if(0 == stat(path, &s)){
		if(S_ISREG(s.st_mode))
			return 1;
	}
	return 0;
}

/*
 * get the parent path, 
 * e.g. ./www/fdler --> ./www/
 */
void get_parent_path(char *path, char *parent) {
	char *p;
	strcpy(parent, path);
	int len = strlen(parent);
	if(parent[len-1] == '/') {
		parent[len-1] = '\0';
	}
	p = strrchr(parent, '/');
	parent[p - parent + 1] = '\0';
}

int block_read(const char *filename, char *buf, int max_size) {
	int n = 0;
	int fd = open(filename, O_RDONLY);
	if(fd == -1) {
		if(conf.log_enable) {
			log_error("open err: %s\n", filename);
		} else {
			fprintf(stderr,"error open %s\n", filename);
		}
		return -1;
	}
	while(1) {
		int t;
		t = read(fd, buf, max_size - n);
		if(t > 0) {
			n +=t;
		} else if(t == -1) {
			if(conf.log_enable) {
				log_error("read\n");
			} else {
				fprintf(stderr,"block read err\n");
			}
		} else if(t == 0) {
			break;
		}
	}
	
	buf[n] = '\0';
	return 0;
}


int dir_html_maker(char *buf, char *path) {
	/*buf - dir_first_part - dir_second_part*/
	int max_len_limit = MAXBUFSIZE - 1024 - 512;

	struct dirent *temp_path;
	struct stat s;
	DIR *dir;
	char newpath[512];

	dir = opendir(path);
	if(dir == NULL){
		//perror("opendir error");
		if(conf.log_enable) {
			log_error("opendir error:%s\n", strerror(errno));
		} else {
			fprintf(stderr,"block read err:%s\n", strerror(errno));
		}
		return -1;
	}

	char prefix[64];
	char *p = strrchr(path, '/');
	strcpy(prefix, p+1);
	int len = strlen(prefix);
	if(len != 0) {
		prefix[len] = '/';
		prefix[len+1] = '\0';
	}

	int pos = 0;
	int ret;
	if(strcmp(path, conf.root_dir) != 0) {
		//add back operation
		if(len == 0) {
			ret = sprintf(buf+pos,"<div class=\"dir\"><a href=\"../\"><img src=\"/.res/back.ico\">&nbsp;BACK</a></div>\n");
		} else {
			ret = sprintf(buf+pos,"<div class=\"dir\"><a href=\"./\"><img src=\"/.res/back.ico\">&nbsp;BACK</a></div>\n");
		}
		pos +=ret;
	}

	int max_item_num = conf.max_sub_item_num;
	//set the maximum value of items
	item_t * items = (item_t *)malloc(max_item_num * sizeof(item_t));
	int item_cnt = 0;

	while((temp_path = readdir(dir))!=NULL) {
		
		if(!strcmp(temp_path->d_name,".")||!strcmp(temp_path->d_name,"..") || !strcmp(temp_path->d_name, ".res"))
			continue;

		if(path[strlen(path)-1]=='/')
			sprintf(newpath, "%s%s", path, temp_path->d_name);
		else
			sprintf(newpath, "%s/%s", path, temp_path->d_name);
		
		lstat(newpath, &s);
		
		if(S_ISDIR(s.st_mode)) {
			sprintf(items[item_cnt].path,"%s", temp_path->d_name);
			items[item_cnt].dir = 1;
			items[item_cnt].size = s.st_size;
			items[item_cnt].m_time = s.st_mtime;
			item_cnt++;
		} else if(S_ISREG(s.st_mode)) {
			sprintf(items[item_cnt].path, "%s", temp_path->d_name);
			items[item_cnt].dir = 0;
			items[item_cnt].size = s.st_size;
			items[item_cnt].m_time = s.st_mtime;
			item_cnt++;
		}

		//make sure the newest item will be included.
		if(item_cnt >= max_item_num) {
			break;
		}
		// if(S_ISDIR(s.st_mode)){
		// 	ret = sprintf(buf+pos,"<div class=\"dir\"><a href=\"%s%s/\"><img src=\"/.res/dir.png\">&nbsp;%s</a></div>\n", \
		// 					prefix, temp_path->d_name, temp_path->d_name);
		// 	pos +=ret;
		// }else if(S_ISREG(s.st_mode)){
			
		// 	ret = sprintf(buf+pos,"<div class=\"file\"><a href=\"%s%s\"><img src=\"/.res/file.ico\">&nbsp;%s</a></div>\n", \
		// 					prefix, temp_path->d_name, temp_path->d_name);
		// 	pos +=ret;
		// }
	}
	closedir(dir);

	qsort(items, item_cnt, sizeof(item_t), items_cmp);
	int i;
	for(i=0; i<item_cnt; i++) { 
		if(pos+256 > max_len_limit)
			break;
		if(items[i].dir) {
			ret = sprintf(buf+pos,"<div class=\"dir\"><a href=\"%s%s/\"><img src=\"/.res/dir.png\">&nbsp;%s</a></div>\n", \
								prefix, items[i].path, items[i].path);
			pos += ret;
		} else {
			ret = sprintf(buf+pos,"<div class=\"file\"><a href=\"%s%s\"><img src=\"/.res/file.ico\">&nbsp;%s</a></div>\n", \
							prefix, items[i].path, items[i].path);
			pos +=ret;
		}
	}

	free(items);
	return 0;
}