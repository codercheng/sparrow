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
	loop->fd_records = (fd_record_t *)malloc(maxevent * sizeof(fd_record_t));
	int i;
	for(i=0; i<maxevent; i++) {
		loop->fd_records[i].active = 0;
		loop->fd_records[i].events = 0;
		loop->fd_records[i].cb_read = NULL;
		loop->fd_records[i].cb_write = NULL;
		loop->fd_records[i].ptr = NULL;
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
	if((loop->fd_records[fd].events & events) == events) {
		if(loop->fd_records[fd].events & EV_READ) {
			loop->fd_records[fd].cb_read = cb;
		}
		if(loop->fd_records[fd].events & EV_WRITE) {
			loop->fd_records[fd].cb_write = cb;
		}
	} else {     /*new add event*/
		if(EV_READ & events) {
			loop->fd_records[fd].events |= events;
			loop->fd_records[fd].cb_read = cb;
		}
		if(EV_WRITE & events) {
			loop->fd_records[fd].events |= events;
			loop->fd_records[fd].cb_write = cb;
		}

		struct epoll_event ev;
		ev.events = loop->fd_records[fd].events;
		if(loop->etmodel) {
			ev.events |= EPOLLET;
		}
		ev.data.fd = fd;
		
		if(loop->fd_records[fd].active) {/*mod*/
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
	

	loop->fd_records[fd].active = 1;
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
	loop->fd_records[fd].active = 0;
	loop->fd_records[fd].events = 0;
	loop->fd_records[fd].cb_read = NULL;
	loop->fd_records[fd].cb_write = NULL;
	loop->fd_records[fd].ptr = NULL;

	return 0;
}
/**
 * stop the events on the fd, not unregister the fd
 */
int ev_stop(ev_loop_t *loop, int fd, EV_TYPE events) {
	/*fd in use, and evnets on fd*/
	if(loop->fd_records[fd].active && (loop->fd_records[fd].events & events)) {
		if((loop->fd_records[fd].events & EV_READ) && (events & EV_READ)) {
			loop->fd_records[fd].events &= (~EV_READ);
		}
		if((loop->fd_records[fd].events & EV_WRITE) && (events & EV_WRITE)) {
			loop->fd_records[fd].events &= (~EV_WRITE);
		}
		
		struct epoll_event ev;
		ev.events = loop->fd_records[fd].events;
		if(loop->etmodel) {
			ev.events |= EPOLLET;
		}
		ev.data.fd = fd;

		if(-1 == epoll_ctl(loop->epfd, EPOLL_CTL_MOD, fd, &ev)) {
			fprintf(stderr, "epoll_ctl mod in ev_stop\n");
			return -1;
		}

		if(!(loop->fd_records[fd].events & EV_READ || loop->fd_records[fd].events &EV_WRITE)) {
			return ev_unregister(loop, fd);
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
			fd_record_t record = loop->fd_records[fd];
			
			if(EV_READ & loop->events[i].events) {
				(*(record.cb_read))(fd, EV_READ);
			}
			/*pre-step may have unregister the fd, make sure the fd is active!*/
			if((EV_WRITE & loop->events[i].events) && loop->fd_records[fd].active) { 
				(*(record.cb_write))(fd, EV_WRITE);
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
		fprintf(stderr, "bind err\n");
	}

	setnonblocking(listen_sock);

	int reuse = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if(-1 == listen(listen_sock, SOMAXCONN)) {
		fprintf(stderr, "listen err\n");
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



