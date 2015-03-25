#ifndef _THREAD_MANAGE_H
#define _THREAD_MANAGE_H

#ifdef __cplusplus
extern "C" 
{
#endif

#include "ev_loop.h"
#include <pthread.h>

	pthread_t *worker_threads_queue;
	ev_loop_t **ev_loop_queue;



	int worker_threads_init(int thread_num);
	/**/
	void worker_threads_destroy();

#ifdef __cplusplus
}
#endif

#endif
