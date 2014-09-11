/**
 * gcc -g -Wall -lpthread bitchttpd.c global.h async_log.h async_log.c ev_loop.c ev_loop.h thread_manage.h thread_manage.c -o demo -lrt
 */

#include "async_log.h"
#include "ev_loop.h"
#include "global.h"
#include "thread_manage.h"

#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <signal.h>
#include <errno.h>

int listen_sock;
ev_loop_t * listen_loop = NULL;

void *accept_sock(int sock, EV_TYPE events);

int main()
{
	log_init(LOG_TIME_OUT_SECOND, MIN_LOG_LEVEL);

	//worker_threads_init(WORKER_THREAD_NUM);
	
	sleep(1);
	signal(SIGPIPE, SIG_IGN);
	listen_sock = tcp_server(LISTEN_PORT);
	printf("listen sock:%d\n", listen_sock);
	listen_loop = ev_create_loop(20, USE_EPOLLET);
	ev_register(listen_loop, listen_sock, EV_READ, accept_sock);
	ev_run_loop(listen_loop);


	int i;
	for(i=0; i<WORKER_THREAD_NUM; i++)//等待线程全部执行完
		pthread_join(worker_threads_queue[i], NULL);

	return 0;
}

void *read_cb(int sock, EV_TYPE events) {
	
}


void *accept_sock(int sock, EV_TYPE events) {
	struct sockaddr_in client_sock;
	socklen_t len = sizeof(client_sock);
	int conn_fd;
	if((conn_fd = accept(sock, (struct sockaddr *)&client_sock, &len)) > 0)	{
		setnonblocking(conn_fd);

		log_info("Got connection from ip:%s, port:%d, conn_fd:%d\n",inet_ntoa(client_sock.sin_addr),ntohs(client_sock.sin_port), conn_fd);
		int reuse = 1;
    	setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

#ifdef USE_TCP_CORK
	    int on = 1;
	    setsockopt(sock, SOL_TCP, TCP_CORK, &on, sizeof(on));
#endif
		ev_register(listen_loop, conn_fd, EV_READ, accept_sock);
		//close(conn_fd);
	}
	if(-1 == conn_fd) {
		if(errno != EAGAIN && errno != ECONNABORTED   \
            			   && errno != EPROTO && errno != EINTR) {//排除accpet到队列完这种返回，这只是读完了，并不是错误
            log_error("accept err");
         }
	}

	return NULL;
}
