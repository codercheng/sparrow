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
	if (ts1.tv_sec > ts2.tv_sec) {
		return 0;
	}
	else if (ts1.tv_sec == ts2.tv_sec) {
		if (ts1.tv_nsec > ts2.tv_nsec) {
			return 0;
		}
		else if (ts1.tv_nsec == ts2.tv_nsec) {
			return 0;
		}
		else {
			return 1;
		}
	}
	else {
		return 1;
	}
}

static
void heap_percolate_up(ev_timer_t **heap, int pos) {
	ev_timer_t *timer = heap[pos];
	while ((pos > 1) && (timer_cmp_lt(timer->ts, heap[PARENT(pos)]->ts))) {
		heap[pos] = heap[PARENT(pos)];
		pos = PARENT(pos);
	}
	heap[pos] = timer;
}

static
void heap_percolate_down(ev_timer_t **heap, int pos, int heap_size) {
	ev_timer_t *timer = heap[pos];
	while (LCHILD(pos) <= heap_size) {
		int s_pos = LCHILD(pos);
		//right child exist and right is smaller 
		if (s_pos + 1 <= heap_size && timer_cmp_lt(heap[s_pos + 1]->ts, heap[s_pos]->ts)) {
			s_pos++;
		}

		if (timer_cmp_lt(timer->ts, heap[s_pos]->ts)) {
			break;
		}
		heap[pos] = heap[s_pos];
		pos = s_pos;
	}
	heap[pos] = timer;
}

static
void heap_add(ev_loop_t *loop, ev_timer_t *timer) {
	loop->heap[++(loop->heap_size)] = timer;
	if (timer == NULL) {
		//		printf("add timer,but timer == null\n");
	}
	//	printf("head_size_now:%d\n", loop->heap_size);
	heap_percolate_up((ev_timer_t **)(loop->heap), loop->heap_size);
}

static
struct timespec double2timespec(double timeout) {
	long long int sec = (long long int)timeout;
	long long int nsec = (long long int)((timeout - (double)sec) * 1000000000);

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	ts.tv_sec += sec;
	ts.tv_nsec += nsec;
	if (ts.tv_nsec >= 1000000000) {
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
	if (loop->heap_size < 1)
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
	loop->heap = (ev_timer_t**)malloc((capacity + 1)*sizeof(ev_timer_t*));
	//memset(heap, 0, sizeof(heap));
	int i;
	for (i = 0; i <= capacity; i++) {
		loop->heap[i] = NULL;
	}
	loop->heap_size = 0;
	loop->heap_capacity = capacity;


	if ((loop->timer_fd = timerfd_create(CLOCK_MONOTONIC, 0)) < 0) {
		printf("create timefd err\n");
	}
	//	printf("tfd:%d\n", loop->timer_fd);
	setnonblocking(loop->timer_fd);

	struct itimerspec newValue;
	bzero(&newValue, sizeof(newValue));
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 0;
	newValue.it_value = ts;
	if (timerfd_settime(loop->timer_fd, 0, &newValue, NULL) < 0) {
		printf("err in settime\n");
	}
	ev_register(loop, loop->timer_fd, EV_READ, check_timer);
}


void add_timer(ev_loop_t *loop, double timeout, cb_timer_t cb,
	uint8_t repeat, uint8_t groupid, void *ptr) {

	if (loop->heap_size >= loop->heap_capacity) {
		ev_timer_t **temp = (ev_timer_t **)malloc((2 * (loop->heap_capacity) + 1)*sizeof(ev_timer_t *));
		if (temp == NULL) {
			fprintf(stderr, "err in add timer when malloc:%s\n", strerror(errno));
			return;
		}
		int i;
		for (i = 0; i < 2 * (loop->heap_capacity) + 1; i++) {
			temp[i] = NULL;
		}
		loop->heap_capacity *= 2;
		for (i = 0; i <= loop->heap_size; i++) {
			temp[i] = loop->heap[i];
		}
		free(loop->heap);
		loop->heap = (void **)temp;
	}
	int fd = (int)ptr;

	struct timespec ts;
	ts = double2timespec(timeout);

	ev_timer_t *timer = (ev_timer_t*)malloc(sizeof(ev_timer_t));
	if (timer == NULL) {
		fprintf(stderr, "malloc error:%s\n", strerror(errno));
		return;
	}
	timer->timeout = timeout;
	timer->ts = ts;
	timer->cb = cb;
	timer->fd = fd;
	timer->repeat = repeat;
	timer->groupid = groupid;

	fd_records[fd].timer_ptr = timer;
	heap_add(loop, timer);

	/* two special conditions which need to settime */
	/* 1. first timer event                         */
	/* 2. the newly add timer is the new heap top   */
	/*    that means new's ts < old heap top's ts   */
	if (loop->heap_size == 1 || heap_top((ev_timer_t **)loop->heap) == timer) {
		ts = tick(loop);
		struct itimerspec newValue;
		bzero(&newValue, sizeof(newValue));
		newValue.it_value = ts;

		if (timerfd_settime(loop->timer_fd, 0, &newValue, NULL) != 0) {
			fprintf(stderr, "ERROR: timerfd_settime error: %s\n", strerror(errno));
		}
	}
}


struct
timespec tick(ev_loop_t *loop) {
	//printf("tick\n");
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	//time now >= heap top
	while (heap_top((ev_timer_t **)(loop->heap)) != NULL && !timer_cmp_lt(ts, heap_top((ev_timer_t **)(loop->heap))->ts)) {
		//////////////////////////////////////////
		//heap != null, and delete all the cb==null timer in the head
		int bcontinue = 0;
		while (heap_top((ev_timer_t **)(loop->heap)) != NULL && heap_top((ev_timer_t **)(loop->heap))->cb == NULL) {
			heap_pop(loop);
			//			printf("+++++++++(cb == null)+++++++\n");
			bcontinue = 1;
		}
		if (bcontinue) {
			continue;
		}
		////////////////////////////////, int *heap_size/////////
		(*(heap_top((ev_timer_t **)(loop->heap))->cb))(loop, heap_top((ev_timer_t **)(loop->heap)));
		if (!heap_top((ev_timer_t **)(loop->heap))->repeat) {
			heap_pop(loop);
		}
		else {
			//coming soon...
			heap_top((ev_timer_t **)(loop->heap))->ts = double2timespec(heap_top((ev_timer_t **)(loop->heap))->timeout);
			heap_percolate_down((ev_timer_t **)(loop->heap), 1, loop->heap_size);
		}
		/* important: update the current time, because you */
		/* never know how long the callback func costs  */
		clock_gettime(CLOCK_MONOTONIC, &ts);
	}
	if (heap_top((ev_timer_t **)(loop->heap)) == NULL) {
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
		return ts;
	}
	
	long int sec_tmp  = heap_top((ev_timer_t **)loop->heap)->ts.tv_sec;
	long int nsec_tmp = heap_top((ev_timer_t **)loop->heap)->ts.tv_nsec;

	if (ts.tv_nsec > heap_top((ev_timer_t **)(loop->heap))->ts.tv_nsec) {
		sec_tmp--;
		nsec_tmp += 1000000000;
	}
	ts.tv_sec = sec_tmp - ts.tv_sec;
	ts.tv_nsec = nsec_tmp - ts.tv_nsec;

	return ts;
}

void* check_timer(ev_loop_t *loop, int tfd, EV_TYPE events) {
	uint64_t data;
	read(loop->timer_fd, &data, 8);

	struct timespec ts;
	ts = tick(loop);
	struct itimerspec newValue;
	bzero(&newValue, sizeof(newValue));
	newValue.it_value = ts;

	int ret;
	ret = timerfd_settime(loop->timer_fd, 0, &newValue, NULL);
	if (ret == -1) {
		printf("timerfd_settime err:%s\n", strerror(errno));
	}
	return NULL;
}

void delete_timer(ev_loop_t *loop, int sockfd) {
	ev_timer_t * timer = (ev_timer_t *)(fd_records[sockfd].timer_ptr);
	if (timer != NULL) {
		timer->cb = NULL;
	}
}
