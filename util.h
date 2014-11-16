#include <string.h>
#include <ctype.h>

char *str_2_lower(char *str, int len) {
	int i;
	for(i=0; i<len; i++) {
		str[i] = tolower(str[i]);
	}
	return str;
}