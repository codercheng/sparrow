#ifndef _FILE_H
#define _FILE_H

typedef struct {
	char path[256];
	time_t m_time;//modify time
	int dir;
	int size;
} item_t;

int isItFolder(const char *path);

int isItFile(const char *path);

void get_parent_path(char *path, char *parent);

int block_read(char *filename, char *buf, int max_size);

int dir_html_maker(char *buf, char *path);


#endif