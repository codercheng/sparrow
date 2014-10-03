/**
 * cat mime.types | awk '{printf "{\"%s\", \"%s\"},\n",v$1, $2;}' >> temp
 */
#include "mime.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmp(const void *a, const void *b) {
	return strcmp((*(mime_type_t *)a).s_type, (*(mime_type_t *)b).s_type);
}

int mime_type_binary_search(mime_type_t *mime_type, int n, char *target) {
	int left = 0;
	int right = n-1;
	while(left <= right) {
		int mid = left + ((right - left)>>1);
		if(strcmp(mime_type[mid].s_type, target) == 0) {
			return mid;
		} else if (strcmp(mime_type[mid].s_type, target) > 0) {
			right = mid -1;
		} else {
			left = mid + 1;
		}
	}
	return -1;
}

int main()
{

	int size = sizeof(mime_type)/sizeof(mime_type_t);
	printf("%d\n",size);
	qsort(mime_type, size, sizeof(mime_type_t), cmp);
	//int i;
	// for(i=0; i<191; i++) {
	// 	printf("%s:%s\n", mime_type[i].l_type, mime_type[i].s_type);	
	// }
	while(1) {
		char target[16];
		scanf("%s", target);
		int i = mime_type_binary_search(mime_type, size, target);
		if(i == -1) {
			printf("not found\n");
		} else {
			printf("%s:%s\n", mime_type[i].l_type, mime_type[i].s_type);
		}
	}
	
	return 0;
}
