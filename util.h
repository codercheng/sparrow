#include <string.h>
#include <ctype.h>

typedef struct {
    const char *key;
    int key_len;
    const char *value;
    int value_len;
} KV;

static 
const char * strnchr(const char *str, const char target, int n) {
	if (str == NULL || n == 0) {
		return NULL;
	}
	
	int i;
	for (i=0; i<n; i++) {
		if(*(str+i) == target) {
			return str+i;
		}
	}
	return NULL;
}


int parse_get_path(const char *path, int path_len,  /*input*/
				const char **action, int *action_len, KV *kvs, int *kvs_num) {/*output*/
	const char *p = strnchr(path, '?', path_len);
	/*not find the '?'*/
	if (p == NULL) {
		*action = path;
		*action_len = path_len;
		*kvs_num = 0;
	}
	else {
		*action = path;
		*action_len = p- path;
		
		int cnt = 0;	/*kv count*/
		const char *p1 = p;
		const char *p2;
		const char *p3;
		int end = 0;
		
		while(1) {
			if(*kvs_num <= cnt) {/*tha array kvs' len is less than b*/
		    	break;
			}
			p2 = strnchr(p1+1, '=', path_len-(p1+1-path));
			if(p2 == NULL) {
				*kvs_num = cnt;
				return -1;
			}
			p3 = strnchr(p2+1, '&', path_len-(p2+1-path));
			if(p3 == NULL) {
				p3 = path+path_len;
				end = 1;
			}
			kvs[cnt].key = p1+1;
			kvs[cnt].key_len = p2-p1-1;
			kvs[cnt].value = p2+1;
			kvs[cnt].value_len = p3-p2-1;
			cnt++;
			
			if(!end) {
				p1 = p3;
			}
			else {
				break;
			}
			
		}
		*kvs_num = cnt;
	}
	return 0;
}

char *str_2_lower(char *str, int len) {
	int i;
	for(i=0; i<len; i++) {
		str[i] = tolower(str[i]);
	}
	return str;
}