

typedef struct {
	char path[256];
	//time_t m_time;//modify time
	int dir;
	int size;
} item_t;

int isItFolder(const char *path);

int isItFile(const char *path);

int block_read(char *filename, char *buf, int max_size);
int dir_html_maker(char *buf, char *path);
