/**
 * gcc -g -Wall -lpthread bitchttpd.c global.h async_log.h async_log.c ev_loop.c ev_loop.h -o demo -lrt
 */

#include "async_log.h"
#include "ev_loop.h"
#include "global.h"
#include "thread_manage.h"


// void *test(int fd, EV_TYPE events);

int main()
{
	log_init(LOG_TIME_OUT_SECOND, MIN_LOG_LEVEL);

	worker_threads_init(WORKER_THREAD_NUM);
	//log_error("this is a test err log!<%s>", "simon");


	// loop = ev_create_loop(10, 0);
	// ev_register(loop, 0, EV_READ, test);

	// ev_run_loop(loop);




	int i;
	for(i=0; i<WORKER_THREAD_NUM; i++)//等待线程全部执行完
		pthread_join(worker_threads_queue[i], NULL);

	return 0;
}


// void *test(int fd, EV_TYPE events) {
// 	log_info("stdin ready!");;
// 	return NULL;
// }