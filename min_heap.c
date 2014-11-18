#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/timerfd.h>

#include "min_heap.h"
#include "ev_loop.h"


#define RSHIFT(x) ((x) >> 1)
#define LSHIFT(x) ((x) << 1)
#define LCHILD(x) LSHIFT(x)
#define RCHILD(x) (LSHIFT(x)|1)
#define PARENT(x) (RSHIFT(x))

static 
int timer_cmp_lt(struct timespec ts1, struct timespec ts2) {
	if(ts1.tv_sec > ts2.tv_sec) {
		return 0;
	} else if(ts1.tv_sec == ts2.tv_sec) {
		if(ts1.tv_nsec > ts2.tv_nsec) {
			return 0;
		} else if(ts1.tv_nsec == ts2.tv_nsec) {
			return 0;
		} else {
			return 1;
		}
	} else {
		return 1;
	}
}

static
void heap_percolate_up(ev_timer_t **heap, int pos) {
	ev_timer_t *timer = heap[pos];
	while((pos > 1) && (timer_cmp_lt(timer->ts, heap[PARENT(pos)]->ts))) {
		heap[pos] = heap[PARENT(pos)];
		pos = PARENT(pos);	
	}
	heap[pos] = timer;
}

static
void heap_percolate_down(ev_timer_t **heap, int pos, int heap_size) {
	ev_timer_t *timer = heap[pos];
	while(LCHILD(pos) <= heap_size) {
		int s_pos = LCHILD(pos);
		//right child exist and right is smaller 
		if(s_pos+1 <= heap_size && timer_cmp_lt(heap[s_pos+1]->ts, heap[s_pos]->ts)) {
			s_pos++;
		}

		if(timer_cmp_lt(timer->ts, heap[s_pos]->ts)) {
			break;	
		}
		heap[pos] = heap[s_pos];
		pos = s_pos;
	}
	heap[pos] = timer;
}

static 
void heap_add(ev_loop_t *loop,  ev_timer_t *timer) {
	loop->heap[++(loop->heap_size)] = timer;
	heap_percolate_up((ev_timer_t **)(loop->heap), loop->heap_size);
}

static 
struct timespec double2timespec(double timeout) {
	int sec = (int)timeout;
	int nsec = (int)((timeout - sec)*1000000000);

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	ts.tv_sec +=sec;
	ts.tv_nsec += nsec;
	if(ts.tv_nsec >= 1000000000) {
		ts.tv_nsec %= 1000000000;
		ts.tv_sec++;
	}
	return ts;
}

static
ev_timer_t* heap_top(ev_timer_t **heap) {
	return heap[1];
}

static
void heap_pop(ev_loop_t *loop) {
	if(loop->heap_size <1)
		return;
	free(loop->heap[1]);
	loop->heap[1] = loop->heap[loop->heap_size];
	loop->heap[loop->heap_size] = NULL;
	loop->heap_size--;
	heap_percolate_down((ev_timer_t **)loop->heap, 1, loop->heap_size);
}


/*********************************************************
 ********************* extern ****************************
 *********************************************************/

//ev_timer_t **
void timer_heap_init(ev_loop_t *loop, int capacity) {
	loop->heap = (ev_timer_t**)malloc((capacity+1)*sizeof(ev_timer_t*));
	//memset(heap, 0, sizeof(heap));
	int i;
	for(i=0; i<=capacity; i++) {
		loop->heap[i] = NULL;
	}
	loop->heap_size = 0;
	loop->heap_capacity = capacity;


	if((loop->timer_fd= timerfd_create(CLOCK_MONOTONIC,0)) < 0) {
		printf("create timefd err\n");
	}
	printf("tfd:%d\n", loop->timer_fd);
	setnonblocking(loop->timer_fd);
	
	struct itimerspec newValue;
	bzero(&newValue,sizeof(newValue));  
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 0;
	newValue.it_value = ts;
    if(timerfd_settime(loop->timer_fd,0,&newValue, NULL) <0) {
        printf("err in settime\n");
	}
	ev_register(loop, loop->timer_fd, EV_READ, check_timer);  
}


void add_timer(ev_loop_t *loop, double timeout, cb_timer_t cb, 
									uint8_t repeat, void *ptr) {
	int fd = (int)ptr;
	printf("fd:%d\n", fd);
	struct timespec ts;
	ts = double2timespec(timeout);

	ev_timer_t *timer = (ev_timer_t*)malloc(sizeof(ev_timer_t));
	if(timer == NULL) {
		fprintf(stderr, "malloc error:%s\n", strerror(errno));
		return;
	}
	timer->timeout = timeout;
	timer->ts = ts;
	timer->cb = cb;
	timer->fd = fd;
	timer->repeat = repeat;
	
	fd_records[fd].timer_ptr = timer;
	heap_add(loop, timer);
	if(loop->heap_size == 1) {
		ts = tick(loop);
	    struct itimerspec newValue;
	    newValue.it_value = ts;
	    timerfd_settime(loop->timer_fd, 0, &newValue, NULL);
	}
}


struct 
timespec tick(ev_loop_t *loop) {
	//printf("tick\n");
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	//time now >= heap top
	while(heap_top((ev_timer_t **)(loop->heap))!=NULL && !timer_cmp_lt(ts, heap_top((ev_timer_t **)(loop->heap))->ts)) {
		//////////////////////////////////////////
		if(heap_top((ev_timer_t **)(loop->heap))->cb == NULL) {
			heap_pop(loop);
			// ts.tv_sec = 0;
			// ts.tv_nsec = 0;
			// return ts;
			continue;
		}
		////////////////////////////////, int *heap_size/////////
		(*(heap_top((ev_timer_t **)(loop->heap))->cb))(loop, heap_top((ev_timer_t **)(loop->heap)));
		if(!heap_top((ev_timer_t **)(loop->heap))->repeat) {
			heap_pop(loop);
		}
		else {
			//coming soon...
			heap_top((ev_timer_t **)(loop->heap))->ts = double2timespec(heap_top((ev_timer_t **)(loop->heap))->timeout);
			heap_percolate_down((ev_timer_t **)(loop->heap), 1, loop->heap_size);
		}
	}
	if(heap_top((ev_timer_t **)(loop->heap)) == NULL) {
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
		return ts;
	}
	if(ts.tv_nsec > heap_top((ev_timer_t **)(loop->heap))->ts.tv_nsec) {
		heap_top((ev_timer_t **)(loop->heap))->ts.tv_sec--;
		heap_top((ev_timer_t **)(loop->heap))->ts.tv_nsec+=1000000000;
	}
	ts.tv_sec = heap_top((ev_timer_t **)(loop->heap))->ts.tv_sec - ts.tv_sec;
	ts.tv_nsec = heap_top((ev_timer_t **)(loop->heap))->ts.tv_nsec - ts.tv_nsec;
	return ts;
}

void* check_timer(ev_loop_t *loop, int tfd, EV_TYPE events) {
	printf("check_timer_out\n");

	uint64_t data;
    read(tfd, &data, 8);

    struct timespec ts;
    ts = tick(loop);
    struct itimerspec newValue;
    newValue.it_value = ts;
    timerfd_settime(tfd, 0, &newValue, NULL);
    return NULL;
}
