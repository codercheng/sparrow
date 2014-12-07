#ifndef _MSG_CACHE_H
#define _MSG_CACHE_H

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct msg_cache_node_ {
	long long mid;
	time_t mtime;
	char mbody[1024];
	struct msg_cache_node_ *next; 
} msg_cache_node_t;

typedef struct {
	msg_cache_node_t *head;
	msg_cache_node_t *tail;

	int cur_size;
	int capacity;
} msg_cache_t;

msg_cache_t* msg_cache_init(int capacity) {
	msg_cache_t *msg_cache = (msg_cache_t *)malloc(sizeof(msg_cache_t));
	msg_cache->head = NULL;
	msg_cache->tail = NULL;
	msg_cache->capacity = capacity;
	msg_cache->cur_size = 0;
	return msg_cache;
}

void msg_cache_add(msg_cache_t *msg_cache, long long mid, time_t mtime, char *mbody) {
	if(msg_cache->cur_size < msg_cache->capacity) {
		msg_cache_node_t *msg_cache_node = (msg_cache_node_t *)malloc(sizeof(msg_cache_node_t));
		msg_cache_node->mid = mid;
		msg_cache_node->mtime = mtime;
		msg_cache_node->next = NULL;
		memset(msg_cache_node->mbody, 0, sizeof(msg_cache_node->mbody));
		strcpy(msg_cache_node->mbody, mbody);

		if (msg_cache->cur_size == 0) {
			msg_cache->head = msg_cache->tail = msg_cache_node;
		} else {
			msg_cache->tail = msg_cache->tail->next = msg_cache_node;
		}
		msg_cache->cur_size++;

	} else {
		msg_cache_node_t *new_head = msg_cache->head->next;
		msg_cache->head->mid = mid;
		msg_cache->head->mtime = mtime;
		memset(msg_cache->head->mbody, 0, sizeof(msg_cache->head->mbody));
		strcpy(msg_cache->head->mbody, mbody);
		msg_cache->head->next = NULL;

		msg_cache->tail = msg_cache->tail->next = msg_cache->head;

		msg_cache->head = new_head;
	}
}


//just for test
void msg_cache_status(msg_cache_t *msg_cache) {
	printf("-------------b-----------\n");
	printf("size/cap:%d/%d\n", msg_cache->cur_size, msg_cache->capacity);
	msg_cache_node_t *tmp = msg_cache->head;

	while(tmp != NULL) {
		printf("id:%lld, time:%ld, body:%s\n", tmp->mid, tmp->mtime, tmp->mbody);
		tmp = tmp->next;
	}
	printf("------------e------------\n");
}



#endif