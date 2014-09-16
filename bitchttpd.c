/**
 * gcc -g -Wall -lpthread bitchttpd.c bitchttpd.h global.h async_log.h async_log.c ev_loop.c ev_loop.h thread_manage.h thread_manage.c mime.h file.c file.h -o demo -lrt
 */

#include "async_log.h"
#include "ev_loop.h"
#include "global.h"
#include "thread_manage.h"
#include "bitchttpd.h"
#include "mime.h"
#include "file.h"

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

char dir_first_part[1024];
char dir_second_part[512];

void *test(int sock, EV_TYPE events) {
	printf("ready stdin\n");
	return NULL;
}

int main()
{
	srand(time(0));
 	work_dir = WORKING_DIR;
	log_init(LOG_TIME_OUT_SECOND, MIN_LOG_LEVEL);

	//for dir
	block_read(DIR_FIRST_PART, dir_first_part, sizeof(dir_first_part));
	block_read(DIR_SECOND_PART, dir_second_part, sizeof(dir_second_part));

	//mime type
	qsort(mime_type, sizeof(mime_type)/sizeof(mime_type_t), sizeof(mime_type_t), cmp);

	worker_threads_init(WORKER_THREAD_NUM);
	
	//sleep(1);
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
	printf("----------------------BEGIN-----------\n");
	printf("sock:%d\n", sock);

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
			ev_unregister(loop, sock);
			clear(sock);
			close(sock);
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
		char filename[256];//full path
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
		time_t last_modified_time;
		int s = lstat(filename, &filestat);
		if(-1 == s)	{
			//HANDLE_ERROR("not a file or a dir");
			log_warn("Not a file or a dir");
			//??????????????????????????????????
			//need send error code to client 404?
			fd_records[sock].http_code = 404;
			//return NULL;
		}
		/**
		 * ??????????????????????????????
		 * coming soon...
		 */
		else if(S_ISDIR(filestat.st_mode)) {
			//log_warn("Not a file");
			printf("---folder---\n");

			fd_records[sock].http_code = DIR_CODE;//1024 represent folder
			//return NULL;
		}


		if(fd_records[sock].http_code == 404) {
			strncpy(filename, prefix, strlen(prefix));
			strncpy(filename+strlen(prefix), "404.html", strlen("404.html")+1);
			lstat(filename, &filestat);
		}

		printf("file name :%s\n", filename);
		int fd = -1;
		if(fd_records[sock].http_code != DIR_CODE) {
			fd = open(filename, O_RDONLY);
			if(fd == -1) {
				printf("server side err!\n");
				ev_unregister(loop, sock);
				close(sock);
				clear(sock);
				return NULL;
			}

			last_modified_time = filestat.st_mtime;
			//process 304 not modified
			char *time_begin = strstr(fd_records[sock].buf, "If-Modified-Since:");
			if(time_begin != NULL) {
				time_begin = time_begin + sizeof("If-Modified-Since:");
				char *time_end = strchr(time_begin, '\n');
					// printf("end - begin :%d-\n", time_end - time_begin + 1);
					// char temp[128];
					// char temp2[128];
					// snprintf(temp, time_end - time_begin + 1, "%s", time_begin);
					// snprintf(temp2, time_end - time_begin + 1, "%s", ctime(&last_modified_time));
					// printf("file time :%s-\n", temp2);
					// printf("web  time :%s-\n", temp);


				if(strncmp(ctime(&last_modified_time), time_begin, time_end - time_begin) == 0 && 0) {
					printf("++++++++ 304 BEGIN ++++++++++\n");
					//fd_records[sock].http_code = 304;
				}
			}
		}

		char content_type[128];
		memset(content_type, 0, sizeof(content_type));

		if(fd_records[sock].http_code != 304) {

			printf("-------sock:%d---fd:%d---total:%d\n", sock, fd, (int)filestat.st_size);
			fd_records[sock].ffd = fd;
			fd_records[sock].read_pos =0;

			if(fd_records[sock].http_code != DIR_CODE) {
				fd_records[sock].total_len = (int)filestat.st_size;
				setnonblocking(fd);
			}
			strcpy(fd_records[sock].path, filename);

			
			char *suffix = strrchr(filename+1, '.');
			if(suffix == NULL) {
				if(fd_records[sock].http_code == DIR_CODE)
					strcpy(content_type, "text/html");
				else
					strcpy(content_type, "text/plain");
			} else {
				int index = mime_type_binary_search(mime_type, sizeof(mime_type)/sizeof(mime_type_t), suffix+1);
				if(index == -1) {
					strcpy(content_type, "text/plain");
				} else {
					strcpy(content_type, mime_type[index].l_type);
				}
			}
		}
		printf("**** content_type:%s\n", content_type);
		int header_length;
		printf("begin to write header to buf\n");
		if(fd_records[sock].http_code == 200 || fd_records[sock].http_code == DIR_CODE) {
			printf("200 0r dir\n");
			//file
			if(fd_records[sock].http_code == 200) {
				printf("200\n");
				header_length = sprintf(fd_records[sock].buf, \
					"%sContent-Type: %s\nContent-Length: %d\nLast-Modified:%sCache-Control: max-age=%d\nConnection: Close\n\n", \
					header_200_ok, content_type, (int)filestat.st_size, ctime(&last_modified_time), CacheControl_MaxAge);

			} else {  /*folder*/
				header_length = sprintf(fd_records[sock].buf, \
					"%sContent-Type: %s\nCache-Control: max-age=%d\nConnection: Close\n\n", \
					header_200_ok, content_type, CacheControl_MaxAge);
			}
		}	
		else if(fd_records[sock].http_code == 404) {
			header_length = sprintf(fd_records[sock].buf, \
				"%sContent-Type: %s\nContent-Length: %d\nConnection: Close\n\n", \
				header_404_not_found, content_type, (int)filestat.st_size);
		}
		else if(fd_records[sock].http_code == 304) {
			header_length = sprintf(fd_records[sock].buf, "%s\n", header_304_not_modified);
		}
		printf("len:%d, read_http_end\n", header_length);
		fd_records[sock].buf[header_length] = '\0';

		ev_stop(loop, sock, EV_READ);
		ev_register(loop, sock, EV_WRITE, write_http_header);
		
	}
	else {
		printf("not a header\n");
		clear(sock);
		ev_unregister(loop, sock);
		close(sock);
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
				printf("write header err\n");
				ev_unregister(loop, sockfd);				
				close(sockfd);
				//clear>>>>>>>>>>??????????????????
				clear(sockfd);
				return NULL;
			}
			break;
		}

		if(fd_records[sockfd].write_pos == strlen(fd_records[sockfd].buf)) {
			printf("[--- here ---]\n");
			fd_records[sockfd].write_pos = 0;
			
			if(fd_records[sockfd].http_code == 304) {
				ev_unregister(loop, sockfd);
				close(sockfd);
				clear(sockfd);
				printf("+++++ 304 END ++++++\n");
				return NULL;
			}

			ev_stop(loop, sockfd, EV_WRITE);
			if(fd_records[sockfd].http_code != DIR_CODE) {
				ev_register(loop, sockfd, EV_WRITE, write_http_body);
			}
			else if(fd_records[sockfd].http_code == DIR_CODE) {

				int r = process_dir_html(fd_records[sockfd].path , sockfd);
				if(r == -1) {
					printf("err when making dir html\n");
					ev_unregister(loop, sockfd);
					close(sockfd);
					clear(sockfd);
					return NULL;
				}
				ev_register(loop, sockfd, EV_WRITE, write_dir_html);
			}
			return NULL;
		}
	}
	//ev_register(loop, sockfd, EV_WRITE, write_http_body);
	return NULL;
}

void *write_dir_html(ev_loop_t *loop, int sockfd, EV_TYPE events) {
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
                ev_unregister(loop, sockfd);
				close(sockfd);
				clear(sockfd);
				//clear>>>>>>>>>>??????????????????
				return NULL;
			}
			break;
		}

		if(fd_records[sockfd].write_pos == strlen(fd_records[sockfd].buf)) {
			printf("[--- here2 ---]\n");
			fd_records[sockfd].write_pos = 0;
			ev_unregister(loop, sockfd);
			close(sockfd);
			clear(sockfd);
			return NULL;
		}
	}

	return NULL;
}
// void *process_dir(ev_loop_t *loop, int sockfd, EV_TYPE events) {
// 	//strcpy(fd_records[sockfd].buf, )
// 	memset(fd_records[sockfd].buf, 0, sizeof(fd_records[sockfd].buf));
// 	int n = sprintf(fd_records[sockfd].buf, "%s", dir_first_part);

// 	int ret = dir_html_maker(fd_records[sockfd].buf+n, )

// 	return NULL;
// }


/**
 * return content_length
 */
int process_dir_html(char *path, int sockfd) {
	
	memset(fd_records[sockfd].buf, 0, sizeof(fd_records[sockfd].buf));
	
	int n = sprintf(fd_records[sockfd].buf, "%s", dir_first_part);
	int ret = dir_html_maker(fd_records[sockfd].buf+n, path);
	if(ret == -1)
		return -1;
	sprintf(fd_records[sockfd].buf+strlen(fd_records[sockfd].buf), "%s", dir_second_part);

	return strlen(fd_records[sockfd].buf);
}

void *write_http_body(ev_loop_t *loop, int sockfd, EV_TYPE events) {
	printf("write http body...\n");
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
			printf("%s--write http body end-----------\n", fd_records[sockfd].path);
	   		ev_unregister(loop, sockfd);
	   		close(sockfd);
	   		clear(sockfd);
	  //   	fd_records[sockfd].ffd = NO_FILE_FD;
			// fd_records[sockfd].write_pos = 0;
			// fd_records[sockfd].total_len = 0;
			// fd_records[sockfd].read_pos = 0;
			// fd_records[sockfd].http_code = 200;	
			// memset(fd_records[sockfd].path, 0, sizeof(fd_records[sockfd].path));
	   		return NULL;
	    }
  	}
	return NULL;
}


void clear(int sockfd) {
	fd_records[sockfd].active = 0;
	fd_records[sockfd].events = 0;
	fd_records[sockfd].cb_read = NULL;
	fd_records[sockfd].cb_write = NULL;
	fd_records[sockfd].ffd = NO_FILE_FD;
	fd_records[sockfd].write_pos = 0;
	fd_records[sockfd].total_len = 0;
	fd_records[sockfd].read_pos = 0;
	fd_records[sockfd].http_code = 200;	
	memset(fd_records[sockfd].path, 0, sizeof(fd_records[sockfd].path));
	memset(fd_records[sockfd].buf, 0, MAXBUFSIZE);
}
