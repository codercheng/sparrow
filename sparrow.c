#include "async_log.h"
#include "ev_loop.h"
#include "global.h"
#include "thread_manage.h"
#include "sparrow.h"
#include "mime.h"
#include "file.h"
#include "config.h"
#include "url.h"

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

int main()
{
	srand(time(0));
	read_config(&conf);

 	work_dir = conf.root_dir;

 	if(conf.log_enable) {
		log_init(conf.log_time_out, conf.log_level);
	}

	//for dir
	block_read(DIR_FIRST_PART, dir_first_part, sizeof(dir_first_part));
	block_read(DIR_SECOND_PART, dir_second_part, sizeof(dir_second_part));

	//mime type
	qsort(mime_type, sizeof(mime_type)/sizeof(mime_type_t), sizeof(mime_type_t), cmp);


	worker_threads_init(conf.worker_thread_num);
	
	signal(SIGPIPE, SIG_IGN);
	listen_sock = tcp_server(conf.listen_port);

	if(listen_sock == -1) {
		if(conf.log_enable) {
			log_error("listen err\n");
		} else {
			fprintf(stderr, "listen error\n");
		}
		return -1;
	}
	listen_loop = ev_create_loop(conf.max_conn, 1);
	int ret = ev_register(listen_loop, listen_sock, EV_READ, accept_sock);
	if(ret == -1) {
		if(conf.log_enable) {
			log_error("register err\n");
		} else {
			fprintf(stderr, "register error\n");
		}
		ev_clear(listen_sock);
		return -1;
	}

	if(conf.log_enable) {
		log_info("sparrow started successfully!\n");
	} else {
		fprintf(stdout, "sparrow started successfully!\n");
	}
	ev_run_loop(listen_loop);

	int i;
	for(i=0; i<conf.worker_thread_num; i++)//等待线程全部执行完
		pthread_join(worker_threads_queue[i], NULL);

	return 0;
}

void *accept_sock(ev_loop_t *loop, int sock, EV_TYPE events) {
	struct sockaddr_in client_sock;
	socklen_t len = sizeof(client_sock);
	int conn_fd;
	while((conn_fd = accept(sock, (struct sockaddr *)&client_sock, &len)) > 0)	{
		/*limit the connection*/
		if(conn_fd >= conf.max_conn) {
			if(conf.log_enable) {
				log_warn("Too many connections come, exceeds the maximum num of the configuration!\n");
			} else {
				fprintf(stderr, "Warn: too many connections come, exceeds the maximum num of the configuration!\n");
			}

			close(conn_fd);
			return NULL;
		}

		setnonblocking(conn_fd);

		//log_info("Got connection from ip:%s, port:%d, conn_fd:%d\n",inet_ntoa(client_sock.sin_addr),ntohs(client_sock.sin_port), conn_fd);
		int reuse = 1;
    	setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    	// 接收缓冲区
		int nRecvBuf= TCP_RECV_BUF;
		setsockopt(conn_fd,SOL_SOCKET,SO_RCVBUF,(const char*)&nRecvBuf,sizeof(int));
		//发送缓冲区
		int nSendBuf= TCP_SEND_BUF;
		setsockopt(conn_fd,SOL_SOCKET,SO_SNDBUF,(const char*)&nSendBuf,sizeof(int));
		
	    if(conf.use_tcp_cork) {
	    	int on = 1;
	    	setsockopt(sock, SOL_TCP, TCP_CORK, &on, sizeof(on));
	    }

		int ret = ev_register(ev_loop_queue[rand()%conf.worker_thread_num], conn_fd, EV_READ, read_http);
		if(ret == -1) {
			if(conf.log_enable) {
				log_error("register err\n");
			} else {
				fprintf(stderr, "ev register err in accept_sock()\n");
			}
			ev_unregister(loop, conn_fd);
			close(conn_fd);
			return NULL;
		}
	}
	if(-1 == conn_fd) {
		if(errno != EAGAIN && errno != ECONNABORTED   \
            			   && errno != EPROTO && errno != EINTR) {//排除accpet到队列完这种返回，这只是读完了，并不是错误
           if(conf.log_enable) {
				log_error("accpet err\n");
			} else {
				fprintf(stderr, "accpet err\n");
			}
       		return NULL;
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
				if(conf.log_enable) {
					log_error("read http err\n");
				} else {
					fprintf(stderr, "read http err\n");
				}
				ev_unregister(loop, sock);
				close(sock);
				return NULL;
			} else {
				break;//read complete
			}
		} else if(nread == 0) {
			//client quit
			ev_unregister(loop, sock);
			close(sock);
			return NULL;
		}
	}

	int header_length = fd_records[sock].read_pos;
	fd_records[sock].buf[header_length] = '\0';

	read_complete =(strstr(buf, "\n\n") != 0) ||(strstr(buf, "\r\n\r\n") != 0);

	if(read_complete) {
		char *path_end = strchr(buf+4, ' ');
		int len = path_end - buf - 4 -1;

		char path[256];
		strncpy(path, buf+1+4, len);
		path[len] = '\0';        //can not forget
		
		//defualt home page
		if(strcmp(path, "") == 0) {
			sprintf(path, "/%s", conf.def_home_page);
		} else {
			/*decode, 解决url中包含中文被转码的问题*/
			url_decode(path, strlen(path));
		}
		
		char *prefix = work_dir;
		char filename[256];//full path
		strncpy(filename, prefix, strlen(prefix));
		strncpy(filename+strlen(prefix), path, strlen(path)+1);

//**************************************************************************
// Dynamic service entry
//**************************************************************************

		
		struct stat filestat;
		time_t last_modified_time;
		int s = lstat(filename, &filestat);
		if(-1 == s)	{
			fd_records[sock].http_code = 404;
		}
		else if(S_ISDIR(filestat.st_mode)) {
			fd_records[sock].http_code = DIR_CODE;
		}


		if(fd_records[sock].http_code == 404) {
			strncpy(filename, prefix, strlen(prefix));
			strncpy(filename+strlen(prefix), "404.html", strlen("404.html")+1);
			lstat(filename, &filestat);
		}

		
		int fd = -1;
		if(fd_records[sock].http_code != DIR_CODE) {
			fd = open(filename, O_RDONLY);
			if(fd == -1) {
				if(conf.log_enable) {
					log_error("can not open file:%s\n", filename);
				} else {
					fprintf(stderr,"can not open file:%s\n", filename);
				}
				ev_unregister(loop, sock);
				close(sock);
				return NULL;
			}

			last_modified_time = filestat.st_mtime;
			//process 304 not modified
			char *time_begin = strstr(fd_records[sock].buf, "If-Modified-Since:");
			if(time_begin != NULL) {
				time_begin = time_begin + sizeof("If-Modified-Since:");
				char *time_end = strchr(time_begin, '\n');

				if(strncmp(ctime(&last_modified_time), time_begin, time_end - time_begin -1) == 0) {
					fd_records[sock].http_code = 304;
				}
			}
		}

		char content_type[128];
		memset(content_type, 0, sizeof(content_type));

		if(fd_records[sock].http_code != 304) {

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

		int header_length = 0;

		if(fd_records[sock].http_code == 200 || fd_records[sock].http_code == DIR_CODE) {
			
			if(fd_records[sock].http_code == 200) {
				/*because ctime() retuan a string end with '\n', so no more '\n' is add below*/
				header_length = sprintf(fd_records[sock].buf, \
					"%sContent-Type: %s\nContent-Length: %d\nLast-Modified:%sCache-Control: max-age=%d\nConnection: Close\n\n", \
					header_200_ok, content_type, (int)filestat.st_size, ctime(&last_modified_time), conf.cache_control_max_age);

			} else {  /*folder*/
				header_length = sprintf(fd_records[sock].buf, \
					"%sContent-Type: %s\nCache-Control: max-age=%d\nConnection: Close\n\n", \
					header_200_ok, content_type, conf.cache_control_max_age);
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
		fd_records[sock].buf[header_length] = '\0';

		int ret;
		ret = ev_stop(loop, sock, EV_READ);
		if(ret == -1) {
			close(sock);
			return NULL;
		}
		ret = ev_register(loop, sock, EV_WRITE, write_http_header);
		if(ret == -1) {
			printf("ev register err in read_http()\n");
			if(conf.log_enable) {
				log_error("ev register err in read_http()\n");
			} else {
				fprintf(stderr,"ev register err in read_http()\n");
			}
			ev_unregister(loop, sock);
			close(sock);
			return NULL;
		}
	}
	else {
		ev_unregister(loop, sock);
		close(sock);
		return NULL;	
	}
	return NULL;
}



void *write_http_header(ev_loop_t *loop, int sockfd, EV_TYPE events){
	if(conf.use_tcp_cork) {
    	int on = 1;
    	setsockopt(sockfd, SOL_TCP, TCP_CORK, &on, sizeof(on));
    }
	while(1) {
		int nwrite;
		nwrite = write(sockfd, fd_records[sockfd].buf+fd_records[sockfd].write_pos, strlen(fd_records[sockfd].buf)-fd_records[sockfd].write_pos);
		fd_records[sockfd].write_pos+=nwrite;
		if(nwrite == -1) {
			if(errno != EAGAIN)
			{
				if(conf.log_enable) {
					log_error("%s\n", strerror(errno));
				} else {
					fprintf(stderr,"%s\n", strerror(errno));
				}
				ev_unregister(loop, sockfd);				
				
				close(sockfd);
				return NULL;
			}
			break;
		}

		if(fd_records[sockfd].write_pos == strlen(fd_records[sockfd].buf)) {
			fd_records[sockfd].write_pos = 0;
			
			if(fd_records[sockfd].http_code == 304) {
				ev_unregister(loop, sockfd);
				close(sockfd);
				return NULL;
			}

			ev_stop(loop, sockfd, EV_WRITE);
			if(fd_records[sockfd].http_code != DIR_CODE) {
				int ret = ev_register(loop, sockfd, EV_WRITE, write_http_body);
				if(ret == -1) {
					if(conf.log_enable) {
						log_error("ev register err\n");
					} else {
						fprintf(stderr,"ev register err in write_http_header1()\n");
					}
					ev_unregister(loop, sockfd);
					close(sockfd);
					return NULL;
				}
			}
			else if(fd_records[sockfd].http_code == DIR_CODE) {

				int r = process_dir_html(fd_records[sockfd].path , sockfd);
				if(r == -1) {
					if(conf.log_enable) {
						log_error("err when making dir html\n");
					} else {
						fprintf(stderr,"err when making dir html\n");
					}
					ev_unregister(loop, sockfd);
					close(sockfd);
					return NULL;
				}
				int ret = ev_register(loop, sockfd, EV_WRITE, write_dir_html);
				if(ret == -1) {
					if(conf.log_enable) {
						log_error("ev register err in write_http_header2()\n");
					} else {
						fprintf(stderr,"ev register err in write_http_header2()\n");
					}
					ev_unregister(loop, sockfd);
					close(sockfd);
					return NULL;
				}
			}
			return NULL;
		}
	}
	return NULL;
}

void *write_dir_html(ev_loop_t *loop, int sockfd, EV_TYPE events) {
	if(conf.use_tcp_cork) {
    	int on = 1;
    	setsockopt(sockfd, SOL_TCP, TCP_CORK, &on, sizeof(on));
    }
	while(1) {
		int nwrite;
		nwrite = write(sockfd, fd_records[sockfd].buf+fd_records[sockfd].write_pos, strlen(fd_records[sockfd].buf)-fd_records[sockfd].write_pos);
		fd_records[sockfd].write_pos+=nwrite;
		if(nwrite == -1) {
			if(errno != EAGAIN)
			{
				if(conf.log_enable) {
					log_error("write dir html%s\n", strerror(errno));
				} else {
					fprintf(stderr,"write dir html%s\n", strerror(errno));
				}
                ev_unregister(loop, sockfd);
				close(sockfd);
				return NULL;
			}
			break;
		}

		if(fd_records[sockfd].write_pos == strlen(fd_records[sockfd].buf)) {
			fd_records[sockfd].write_pos = 0;
			ev_unregister(loop, sockfd);
			close(sockfd);
			return NULL;
		}
	}

	return NULL;
}

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
	int ffd = fd_records[sockfd].ffd;
	while(1) {
	    off_t offset = fd_records[sockfd].read_pos;
	    int s = sendfile(sockfd, ffd, &offset, fd_records[sockfd].total_len - fd_records[sockfd].read_pos);
	    fd_records[sockfd].read_pos = offset;
	    if(s == -1) {
			if(errno != EAGAIN) {
				if(conf.log_enable) {
					log_error("%s\n", strerror(errno));
				} else {
					fprintf(stderr,"sendfile:%s\n", strerror(errno));
				}
		   		ev_unregister(loop, sockfd);
		   		close(sockfd);
		   		close(ffd);

				return NULL;
			} else {
				// 写入到缓冲区已满了
				//return NULL;
				break;
			}
	    }
	    if(fd_records[sockfd].read_pos == fd_records[sockfd].total_len) {
	   		ev_unregister(loop, sockfd);
	  		close(sockfd);
	   		close(ffd);

	   		return NULL;
	    }
  	}
	return NULL;
}
