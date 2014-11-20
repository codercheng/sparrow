#include "ev_loop.h"
#include "async_log.h"
#include "thread_manage.h"
#include "global.h"
#include "sparrow.h"
#include "config.h"

#include "min_heap.h"

#include <pthread.h>
#include <stdlib.h>

#include <stdio.h>


static
void * worker_threads_entrance(void *arg) {
	ev_loop_t *loop = (ev_loop_t *)arg;
	if(loop == NULL) {
		if(conf.log_enable) {
			log_error("loop is empty\n");
		} else {
			fprintf(stderr,"loop is empty\n");
		}
	}
	ev_run_loop(loop);
	return NULL;
}
void worker_threads_destroy() {

}

int worker_threads_init(int thread_num) {
	//log_info("enter worker_init...");
	worker_threads_queue = (pthread_t *)malloc(thread_num * sizeof(pthread_t));
	ev_loop_queue = (ev_loop_t **)malloc(thread_num * sizeof(ev_loop_t*));

	int i, ret;
	for(i=0; i<thread_num; i++) {
		
		ev_loop_queue[i] = ev_create_loop(conf.max_conn, conf.use_epoll_et);
		timer_heap_init(ev_loop_queue[i], conf.max_conn);

		ret = pthread_create(&(worker_threads_queue[i]), NULL, worker_threads_entrance, (void *)ev_loop_queue[i]);
		if(ret < 0)	{
			if(conf.log_enable) {
				log_error("thread init create err\n");
			} else {
				fprintf(stderr,"thread init create err\n");
			}
			worker_threads_destroy();
			return -1;
		}
	}
	return 0;
}


