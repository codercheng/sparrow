/**
 * gcc -g -Wall -lpthread bitchttpd.c bitchttpd.h global.h async_log.h async_log.c ev_loop.c ev_loop.h thread_manage.h thread_manage.c -o demo -lrt
 */

#include "async_log.h"
#include "ev_loop.h"
#include "global.h"
#include "thread_manage.h"
#include "bitchttpd.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <signal.h>
#include <errno.h>
#include <sys/sendfile.h>
#include <time.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

char *work_dir;

int listen_sock;
ev_loop_t * listen_loop = NULL;

void *test(int sock, EV_TYPE events) {
	printf("ready stdin\n");
	return NULL;
}

int main()
{
	srand(time(0));
 	work_dir = WORKING_DIR;
	log_init(LOG_TIME_OUT_SECOND, MIN_LOG_LEVEL);

	worker_threads_init(WORKER_THREAD_NUM);
	
	sleep(1);
	signal(SIGPIPE, SIG_IGN);
	listen_sock = tcp_server(LISTEN_PORT);
	printf("listen sock:%d\n", listen_sock);
	if(listen_sock == -1) {
		log_error("listen err");
		return -1;
	}
	listen_loop = ev_create_loop(MAX_EVENT, 0);
	ev_register(listen_loop, listen_sock, EV_READ, accept_sock);
	//ev_register(listen_loop, 0, EV_READ, test);
	ev_run_loop(listen_loop);


	int i;
	for(i=0; i<WORKER_THREAD_NUM; i++)//等待线程全部执行完
		pthread_join(worker_threads_queue[i], NULL);

	return 0;
}

void *accept_sock(ev_loop_t *loop, int sock, EV_TYPE events) {
	struct sockaddr_in client_sock;
	socklen_t len = sizeof(client_sock);
	int conn_fd;
	while((conn_fd = accept(sock, (struct sockaddr *)&client_sock, &len)) > 0)	{
		setnonblocking(conn_fd);

		log_info("Got connection from ip:%s, port:%d, conn_fd:%d\n",inet_ntoa(client_sock.sin_addr),ntohs(client_sock.sin_port), conn_fd);
		int reuse = 1;
    	setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    	// 接收缓冲区
		int nRecvBuf= TCP_RECV_BUF;
		setsockopt(conn_fd,SOL_SOCKET,SO_RCVBUF,(const char*)&nRecvBuf,sizeof(int));
		//发送缓冲区
		int nSendBuf= TCP_SEND_BUF;
		setsockopt(conn_fd,SOL_SOCKET,SO_SNDBUF,(const char*)&nSendBuf,sizeof(int));


#ifdef USE_TCP_CORK
	    int on = 1;
	    setsockopt(sock, SOL_TCP, TCP_CORK, &on, sizeof(on));
#endif
		//ev_register(listen_loop, conn_fd, EV_READ, accept_sock);
		//close(conn_fd);
		ev_register(ev_loop_queue[rand()%WORKER_THREAD_NUM], conn_fd, EV_READ, read_http);
	}
	if(-1 == conn_fd) {
		if(errno != EAGAIN && errno != ECONNABORTED   \
            			   && errno != EPROTO && errno != EINTR) {//排除accpet到队列完这种返回，这只是读完了，并不是错误
            log_error("accept err");
         }
	}

	return NULL;
}
void *read_http(ev_loop_t *loop, int sock, EV_TYPE events) {
	char *buf = fd_records[sock].buf;
	int read_complete = 0;

	int nread = 0;

	while(1) {
		nread = read(sock, buf+fd_records[sock].read_pos, MAXBUFSIZE - fd_records[sock].read_pos);
		if(nread > 0) {
			fd_records[sock].read_pos += nread;
		}
		else if(nread == -1) {
			if(errno != EAGAIN)	{
				printf("err in read_http\n");
			} else {
				break;//read complete
			}
		} else if(nread == 0) {
			//client quit
			printf("+++++++++client quit\n");
			
			fd_records[sock].ffd = NO_FILE_FD;
			fd_records[sock].write_pos = 0;
			fd_records[sock].total_len = 0;
			fd_records[sock].read_pos = 0;
			return NULL;
		}
	}

	int header_length = fd_records[sock].read_pos;
	fd_records[sock].buf[header_length] = '\0';

	read_complete =(strstr(buf, "\n\n") != 0) ||(strstr(buf, "\r\n\r\n") != 0);

	if(read_complete) {
#ifdef _DEBUG_
		printf("html:\n%s\n---------\n",buf);
#endif
		char *path_end = strchr(buf+4, ' ');
		int len = path_end - buf - 4 -1;

		char path[256];
		strncpy(path, buf+1+4, len);
		path[len] = '\0';        //can not forget
		

		char *prefix = work_dir;
		char filename[256];
		strncpy(filename, prefix, strlen(prefix));
		strncpy(filename+strlen(prefix), path, strlen(path)+1);
		//filename[]
#ifdef _DEBUG_
		printf("path:%s\n",path);//cout<<"path:"<<path<<endl;
		printf("prefix:%s\n", prefix);
		printf("filename:%s--\n", filename);
		printf("***%d***\n", sock);
#endif
//**************************************************************************
// Dynamic service entry
//**************************************************************************

		
		struct stat filestat;
		int s = lstat(filename, &filestat);
		if(-1 == s)	{
			//HANDLE_ERROR("not a file or a dir");
			log_warn("Not a file or a dir");
			//??????????????????????????????????
			//need send error code to client 404?
			return NULL;
		}
		/**
		 * ??????????????????????????????
		 * coming soon...
		 */
		if(S_ISDIR(filestat.st_mode)) {
			log_warn("Not a file");
			return NULL;
		}

		int fd = open(filename, O_RDONLY);
		if(-1 == fd) {
			//info_manager.give_back_node(info);
			log_warn("can not open file");
			printf("can not open file\n");
			return NULL;
		}
		printf("-------sock:%d---fd:%d---total:%d\n", sock, fd, (int)filestat.st_size);
		fd_records[sock].ffd = fd;
		fd_records[sock].read_pos =0;
		fd_records[sock].total_len = (int)filestat.st_size;

		//sprintf(fd_records[sock].buf, "%sContent-Length: %d\r\n\r\n", header_200_start, (int)filestat.st_size);
		ev_stop(loop, sock, EV_READ);
		ev_register(loop, sock, EV_WRITE, write_http_body);
		
	}
	else {
		printf("not a header\n");
		fd_records[sock].ffd = NO_FILE_FD;
		fd_records[sock].write_pos = 0;
		fd_records[sock].total_len = 0;
		fd_records[sock].read_pos = 0;
		//info_manager.give_back_node(info);
		return NULL;	
	}
	return NULL;
}

void *write_http_header(ev_loop_t *loop, int sockfd, EV_TYPE events){
	printf("write http header ...\n");
#ifdef USE_TCP_CORK
    int on = 0;
    setsockopt(sockfd, SOL_TCP, TCP_CORK, &on, sizeof(on));
#endif
	while(1) {
		int nwrite;
		nwrite = write(sockfd, fd_records[sockfd].buf+fd_records[sockfd].write_pos, strlen(fd_records[sockfd].buf)-fd_records[sockfd].write_pos);
		fd_records[sockfd].write_pos+=nwrite;
		if(nwrite == -1) {
			if(errno != EAGAIN)
			{
				log_error("write header");

				close(sockfd);
				//clear>>>>>>>>>>??????????????????
				return NULL;
			}
			break;
		}

		if(fd_records[sockfd].write_pos == strlen(fd_records[sockfd].buf)) {
			fd_records[sockfd].write_pos = 0;
			return NULL;
		}
	}
	//ev_register(loop, sockfd, EV_WRITE, write_http_body);
	return NULL;
}
void *write_http_body(ev_loop_t *loop, int sockfd, EV_TYPE events) {
	printf("write body\n");
	int ffd = fd_records[sockfd].ffd;
	//printf("sock:%d---ffd:%d---total:%d\n", sockfd, ffd, fd_records[sockfd].total_len);
	while(1) {
	    off_t offset = fd_records[sockfd].read_pos;
	    //printf("offset:%d\n", offset);
	    int s = sendfile(sockfd, ffd, &offset, fd_records[sockfd].total_len - fd_records[sockfd].read_pos);
	    fd_records[sockfd].read_pos = offset;
	   // printf("2_offset:%d\n", offset);
	    if(s == -1) {
	    	//printf("here s = -1\n");
			if(errno != EAGAIN) {
				//HANDLE_ERROR("sendfile");
				log_error("sendfile");
				return NULL;
			} else {
				// 写入到缓冲区已满了
				return NULL;
			}
	    }
	    if(fd_records[sockfd].read_pos == fd_records[sockfd].total_len) {
	    	//printf("over\n");
	      	// 读写完毕
	   		close(ffd);
	   		//>>>>>>>>>
	   		ev_unregister(loop, sockfd);
	   		close(sockfd);
	    	fd_records[sockfd].ffd = NO_FILE_FD;
			fd_records[sockfd].write_pos = 0;
			fd_records[sockfd].total_len = 0;
			fd_records[sockfd].read_pos = 0;	
	   		return NULL;
	    }
  	}
	return NULL;
}