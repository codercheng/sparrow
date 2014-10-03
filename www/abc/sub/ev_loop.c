/***********************************************************************
 * @author simoncheng
 * @data 2014.8
 * @version v0.1
 * this program is an encapsulation of the EPOLL API which is frequently
 * used in backend systems based on linux.
 * it likes a simplified libev library which only supports the events 
 * on fds. timer and signal will be added later.
 ***********************************************************************/


#include "ev_loop.h"
//#include "bitchttpd.h"

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#include <string.h>
#include <stdint.h>

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netinet/in.h>

static int lock_ = 0;

/**
 * create and init an event loop
 * @param  maxevent max count event(fd) the loop support
 * @param  et       whether use EPOLLET
 */
ev_loop_t *ev_create_loop(int maxevent, int et) {
	ev_loop_t *loop;
	loop = (ev_loop_t*)malloc(sizeof(ev_loop_t));
	loop->maxevent = maxevent;
	loop->etmodel = et;
	loop->epfd = epoll_create1(0);
	if(loop->epfd == -1) {
		return NULL;
	}
	loop->events = (struct epoll_event *)malloc(maxevent * sizeof(struct epoll_event));

	//init
	LOCK(lock_);
	if(fd_records == NULL) {
		fd_records = (fd_record_t *)malloc(maxevent * sizeof(fd_record_t));
		printf("init\n");
	}
	printf("after init\n");
	UNLOCK(lock_);
	int i;
	for(i=0; i<maxevent; i++) {
		fd_records[i].active = 0;
		fd_records[i].events = 0;
		fd_records[i].cb_read = NULL;
		fd_records[i].cb_write = NULL;

		fd_records[i].ffd = NO_FILE_FD;
		fd_records[i].write_pos = 0;
		fd_records[i].read_pos = 0;
		fd_records[i].total_len = 0;
		memset(fd_records[i].buf, 0, MAXBUFSIZE);
		// if(ptr_size == 0)
		// 	fd_records[i].ptr = NULL;
		// else {
		// 	fd_records[i].ptr = (void *)malloc(sizeof(sock_info_t));
		// 	//memset(fd_records[i].ptr, 0, ptr_size);
		// }

	}
	return loop;
}

/**
 * register events on fd in the loop
 */
int ev_register(ev_loop_t*loop, int fd, EV_TYPE events, cb_func_t cb) {
	if(!(events & EV_READ || events & EV_WRITE)) {
		fprintf(stderr, "invalid events\n");
		return -1;
	}
	if(fd < 0) {
		fprintf(stderr, "invalid fd\n");
		return -1;
	}

	/**
	 * events registerd already, just change the cb
	 */
	if((fd_records[fd].events & events) == events) {
		printf("ev_registered already\n");;
		if(fd_records[fd].events & EV_READ) {
			fd_records[fd].cb_read = cb;
		}
		if(fd_records[fd].events & EV_WRITE) {
			printf("+++++++++EV_WRITE++++++++\n");
			fd_records[fd].cb_write = cb;
		}
	} else {     /*new add event*/
		if(EV_READ & events) {
			fd_records[fd].events |= events;
			fd_records[fd].cb_read = cb;
		}
		if(EV_WRITE & events) {
			fd_records[fd].events |= events;
			fd_records[fd].cb_write = cb;
		}

		struct epoll_event ev;
		ev.events = fd_records[fd].events;
		if(loop->etmodel) {
			ev.events |= EPOLLET;
		}
		ev.data.fd = fd;
		
		if(fd_records[fd].active) {/*mod*/
			if(-1 == epoll_ctl(loop->epfd, EPOLL_CTL_MOD, fd, &ev)) {
				fprintf(stderr, "epoll_ctl mod in ev_register\n");
				return -1;
			}
		} else {/*add*/
			if(-1 == epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev)) {
				fprintf(stderr, "epoll_ctl add in ev_register\n");
				return -1;
			}
		}

	}
	

	fd_records[fd].active = 1;
	return 0;
}

/**
 * unregister the fd from the loop
 */
int ev_unregister(ev_loop_t *loop, int fd) {
	struct epoll_event ev;
	if(-1 == epoll_ctl(loop->epfd, EPOLL_CTL_DEL, fd, &ev)) {
		fprintf(stderr, "epoll_ctl mod in ev_unregister\n");
		return -1;
	}
	fd_records[fd].active = 0;
	fd_records[fd].events = 0;
	fd_records[fd].cb_read = NULL;
	fd_records[fd].cb_write = NULL;
	//fd_records[fd].ptr = NULL;
	//
	fd_records[fd].ffd = NO_FILE_FD;
	fd_records[fd].write_pos = 0;
	fd_records[fd].read_pos = 0;
	fd_records[fd].total_len = 0;
	memset(fd_records[fd].buf, 0, MAXBUFSIZE);

	return 0;
}
/**
 * stop the events on the fd, not unregister the fd
 */
int ev_stop(ev_loop_t *loop, int fd, EV_TYPE events) {

	printf("1>%d\n", fd_records[fd].events);
	/*fd in use, and evnets on fd*/
	if(fd_records[fd].active && (fd_records[fd].events & events)) {
		if((fd_records[fd].events & EV_READ) && (events & EV_READ)) {
			fd_records[fd].events &= (~EV_READ);
		}
		if((fd_records[fd].events & EV_WRITE) && (events & EV_WRITE)) {
			fd_records[fd].events &= (~EV_WRITE);
		}
		printf("2>%d\n", fd_records[fd].events);

		struct epoll_event ev;
		ev.events = fd_records[fd].events;
		if(loop->etmodel) {
			ev.events |= EPOLLET;
		}
		ev.data.fd = fd;

		if(-1 == epoll_ctl(loop->epfd, EPOLL_CTL_MOD, fd, &ev)) {
			fprintf(stderr, "epoll_ctl mod in ev_stop\n");
			return -1;
		}
		printf("3>%d\n", fd_records[fd].events);
		if(!(fd_records[fd].events & EV_READ || fd_records[fd].events &EV_WRITE)) {
			printf("here...\n");
			//return ev_unregister(loop, fd);
		}
	}
	return 0;
}

/**
 * run loop, this is start of the loop
 */
int ev_run_loop(ev_loop_t *loop) {
	while(1) {
		int num;
		num = epoll_wait(loop->epfd, loop->events, loop->maxevent, -1);
		if(num == -1) {
			fprintf(stderr, "epoll wait error\n");
			return -1;
		}
		int i;
		for(i=0; i<num; i++) {
			int fd = loop->events[i].data.fd;
			fd_record_t record = fd_records[fd];
			
			if(EV_READ & loop->events[i].events) {
				(*(record.cb_read))(loop, fd, EV_READ);
			}
			/*pre-step may have unregisterd the fd, make sure the fd is active!*/
			if((EV_WRITE & loop->events[i].events) && fd_records[fd].active) { 
				(*(record.cb_write))(loop, fd, EV_WRITE);
			}
			
		}
	}
}

/**
 * a build-in tcp server, return the listen socket
 */
int tcp_server(int port) {
	signal(SIGPIPE, SIG_IGN);
	struct sockaddr_in server_addr;
	int listen_sock;

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(-1 == listen_sock) {
		fprintf(stderr, "listen err\n");
	}

	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_port = htons(port);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	int ret;
	ret = bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if(-1 == ret) {
		fprintf(stderr, "bind err: %s\n", strerror(errno));
		return -1;
	}

	setnonblocking(listen_sock);

	int reuse = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if(-1 == listen(listen_sock, SOMAXCONN)) {
		fprintf(stderr, "listen err\n");
		return -1;
	}
	//printf("listen fd:%d\n", listen_sock);
	return listen_sock;

}

/**
 * set a fd to non-blocking
 */
int setnonblocking(int fd) {
  int flags;
  if (-1 ==(flags = fcntl(fd, F_GETFL, 0))) {
  	perror("setnonblocking error");
  	return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}



