/***********************************************************************
 * @author simoncheng
 * @data 2014.8
 * @version v0.1
 * this program is an encapsulation of the EPOLL API which is frequently
 * used in backend systems based on linux.
 * it likes a simplified libev library which only supports the events 
 * on fds. timer and signal will be added later.
 ***********************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef _EV_LOOP_H
#define _EV_LOOP_H

#include <unistd.h>
#include <sys/epoll.h>
#include "global.h"

#define EV_TYPE __uint32_t


/**
 * use when init fd_records in muti_threads environment
 */
#define LOCK(Lock)  while(!__sync_bool_compare_and_swap(&Lock, 0, 1))
#define UNLOCK(Lock) Lock = 0

enum {
	EV_READ = EPOLLIN,
	EV_WRITE = EPOLLOUT
};




typedef struct {
	int epfd;
	int maxevent;
	int etmodel;
	//fd_record_t *fd_records;
	struct epoll_event *events;
}ev_loop_t;

typedef void* (*cb_func_t) (ev_loop_t *loop, int fd, EV_TYPE events);

typedef struct {
	int active;
	
	EV_TYPE events;
	cb_func_t cb_read;
	cb_func_t cb_write; 	

	int ffd;
	unsigned int write_pos;
	unsigned int read_pos;
	unsigned int total_len;
	char buf[MAXBUFSIZE];
	int http_code;
	char path[256];
	//void *ptr; /*reserved pointer*/
}fd_record_t;

//muti-threads share the fd_records
fd_record_t *fd_records;

ev_loop_t *ev_create_loop(int maxevent, int et);
int ev_register(ev_loop_t*loop, int fd, EV_TYPE events, cb_func_t cb);
int ev_unregister(ev_loop_t *loop, int fd);
int ev_stop(ev_loop_t *loop, int fd, EV_TYPE events);
int ev_run_loop(ev_loop_t *loop);


int tcp_server(int port);
int setnonblocking(int fd);


#ifdef __cplusplus
}
#endif

#endif