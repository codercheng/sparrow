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
#include <ctype.h>

#include "util.h"
#include "min_heap.h"
#include "cJSON.h"
#include "picohttpparser.h"

char *work_dir;

int listen_sock;
ev_loop_t * listen_loop = NULL;

char dir_first_part[1024];
char dir_second_part[512];

unsigned long long int round_robin_num = 0;

static 
int str_equal(char* str, size_t len, const char* t)
{
  return memcmp(str_2_lower(str, len), t, len) == 0;
}

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


//for debug
void dbg_printf(const char *str) {
#ifdef _DEBUG
		printf("[%s:%d] %s\n", __FILE__, __LINE__, str);
#endif
}

static 
void process_timeout(ev_loop_t *loop, ev_timer_t *timer) {
	if(fd_records[timer->fd].active) {
		ev_unregister(loop, timer->fd);
	}
	close(timer->fd);
}

static
void safe_close(ev_loop_t *loop, int sockfd) {
	delete_timer(loop, sockfd);
	ev_unregister(loop, sockfd);
	close(sockfd);
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

		if(conf.log_enable) {
			log_info("Got connection from ip:%s, port:%d, conn_fd:%d\n",inet_ntoa(client_sock.sin_addr),ntohs(client_sock.sin_port), conn_fd);
		}else {
			//printf("ip:%s, conn_fd:%d\n", inet_ntoa(client_sock.sin_addr),conn_fd);
		}
		int reuse = 1;
    	setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    	// 接收缓冲区
		int nRecvBuf= TCP_RECV_BUF;
		setsockopt(conn_fd,SOL_SOCKET,SO_RCVBUF,(const char*)&nRecvBuf,sizeof(int));
		//发送缓冲区
		int nSendBuf= TCP_SEND_BUF;
		setsockopt(conn_fd,SOL_SOCKET,SO_SNDBUF,(const char*)&nSendBuf,sizeof(int));
		
	    // if(conf.use_tcp_cork) {
	    // 	int on = 1;
	    // 	setsockopt(sock, SOL_TCP, TCP_CORK, &on, sizeof(on));
	    // }
		int ret = ev_register(ev_loop_queue[(round_robin_num++)%conf.worker_thread_num/*rand()%conf.worker_thread_num*/], conn_fd, EV_READ, read_http);
		if(ret == -1) {
			if(conf.log_enable) {
				log_error("register err\n");
			} else {
				fprintf(stderr, "ev register err in accept_sock()\n");
			}
			//ev_unregister(loop, conn_fd);
			//close(conn_fd);
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
	if(sock > conf.max_conn) {
		safe_close(loop, sock);
		return NULL;
	}

	char *buf = fd_records[sock].buf;
	int read_complete = 0; /*判断是否读取完 \r\n\r\n*/

	////////////////////////////////////////////////////////////////////
	const char *method, *path;
	int pret, minor_version;
	struct phr_header headers[100];
	size_t  method_len, path_len, num_headers;
	ssize_t nread;
	////////////////////////////////////////////////////////////////////
	
	while(1) {
		nread = read(sock, buf+fd_records[sock].read_pos, MAXBUFSIZE - fd_records[sock].read_pos);
		if(nread > 0) {
			read_complete =(strstr(buf+fd_records[sock].read_pos, "\n\n") != 0) 
			||(strstr(buf+fd_records[sock].read_pos, "\r\n\r\n") != 0);
			
			fd_records[sock].read_pos += nread;
			//判断是否读取完 \r\n\r\n
			if(read_complete) {
				break;
			}
			//问题又来了，如果对方迟迟都没有发\r\n\r\n那么岂不是要一直等下去？
			//加一个定时器
			//break;
		}
		else if(nread == -1) {
			if(errno != EAGAIN)	{
				if(conf.log_enable) {
					log_error("read http err, %s\n", strerror(errno));
				} else {
					fprintf(stderr, "read http err, %s\n", strerror(errno));
				}
				//是否需要处理timer呢????
				safe_close(loop, sock);
				return NULL;
			} else {
				//这个地方应该是返回，等下一次触发继续读
				return NULL;
				//break;//read complete
			}
		} else if(nread == 0) {
			dbg_printf("client quit!");
			safe_close(loop, sock);
			return NULL;
		}
	}

	
	int header_length = fd_records[sock].read_pos;
	fd_records[sock].buf[header_length] = '\0';
	
	num_headers = sizeof(headers) / sizeof(headers[0]);
	pret = phr_parse_request(buf, header_length, &method, &method_len, &path, &path_len,
                             &minor_version, headers, &num_headers, 0);
	if(pret < 0) {
		safe_close(loop, sock);
		return NULL;
	}
	int i;
#ifdef _DEBUG
	printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	printf("request is %d bytes long\n", pret);
	printf("method is %.*s\n", (int)method_len, method);
	printf("path is %.*s\n", (int)path_len, path);
	printf("HTTP version is 1.%d\n", minor_version);
	printf("headers:\n");
	for (i = 0; i != (int)num_headers; ++i) {
	    printf("%.*s: %.*s\n", (int)headers[i].name_len, headers[i].name,
	           (int)headers[i].value_len, headers[i].value);
	}
	printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
#endif

	if(conf.log_enable) {
		//log the info
		log_info("+++++++++++++++++++++++ REQUEST START +++++++++++++++++++++\n");
		log_info("request is %d bytes long\n", pret);
		log_info("method is %.*s\n", (int)method_len, method);
		log_info("path is %.*s\n", (int)path_len, path);
		log_info("HTTP version is 1.%d\n", minor_version);
		log_info("headers:\n");
		for (i = 0; i != (int)num_headers; ++i) {
		    log_info("%.*s: %.*s\n", (int)headers[i].name_len, headers[i].name,
		           (int)headers[i].value_len, headers[i].value);
		}
		log_info("++++++++++++++++++++++++ REQUEST END ++++++++++++++++++++++\n");
	}

	if(read_complete) {
		//目前暂时只支持Get，排除非GET外的其他请求
		if(!str_equal((char *)method, method_len, "get")) {
			safe_close(loop, sock);
			return NULL;
		}
		
		//the last modified time of file cached in browser side
		const char *last_mtime = NULL;
		size_t last_mtime_len = 0;
		
		//处理Keep-alive和modified time
		for (i = 0; i != (int)num_headers; ++i) {
		    if (str_equal((char *)headers[i].name, headers[i].name_len, "connection")  &&
		       	str_equal((char *)headers[i].value, headers[i].value_len, "keep-alive")) {
		    
		    	fd_records[sock].keep_alive = 1;
		    	dbg_printf("keep_alive connection!");
		    }
		    if (str_equal((char *)headers[i].name, headers[i].name_len, "if-modified-since")) {
		    	last_mtime = headers[i].value;
		    	last_mtime_len = headers[i].value_len;
		    	dbg_printf("find last_modified_time!");
		    }
		}
		
		const char *action;
   		int action_len;
   		KV kvs[0]; // not used
  		int  kvs_num = sizeof(kvs)/sizeof(kvs[0]);
   		
   		int p_ret = parse_get_path(path, path_len, &action, &action_len, kvs, &kvs_num);
   		if (p_ret == -1) {
   			safe_close(loop, sock);
			return NULL;
   		}

		char *prefix = work_dir;
		char filename[1024 + 1 + strlen(work_dir)];//full path
		memset(filename, 0, sizeof(filename));
		
		if(memcmp(action, "/", action_len) == 0) {
			sprintf(filename, "%s/%s", prefix, conf.def_home_page);
		} else {
			sprintf(filename, "%s%.*s", prefix, action_len, action);
		}
#ifdef _DEBUG
		char dbg_msg[512];
		memset(dbg_msg, 0, sizeof(dbg_msg));
		sprintf(dbg_msg, "prefix:%s", prefix);

		dbg_printf(dbg_msg);

		memset(dbg_msg, 0, sizeof(dbg_msg));
		sprintf(dbg_msg, "fileFullPath:%s", filename);
		dbg_printf(dbg_msg);
#endif
		/***********************************************************************
		 *decode, 解决url中包含中文/特殊字符"&%.."被转码的问题
		 *这一步可以加到decode特定path的内容的时候用到
		 * 直接加到http_parse_path()中去
		 **********************************************************************/
		//url_decode(path, path_len);
		
		
		struct stat filestat;
		time_t last_modified_time;

		/***********************************************************************
		 *decode, 解决url中包含中文/特殊字符"&%.."被转码的问题
		 *这一步可以加到decode特定path的内容的时候用到
		 * 直接加到http_parse_path()中去
		 **********************************************************************/
		url_decode(filename, strlen(filename));
		int s = lstat(filename, &filestat);
		if(-1 == s)	{
			fd_records[sock].http_code = 404;
		}
		else if(S_ISDIR(filestat.st_mode)) {
			fd_records[sock].http_code = DIR_CODE;
		}


		if(fd_records[sock].http_code == 404) {
			memset(filename, 0, sizeof(filename));
			sprintf(filename, "%s/%s", prefix, "404.html");
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
				safe_close(loop, sock);
				return NULL;
			}
			
			last_modified_time = filestat.st_mtime;
			//process 304 not modified
			if(last_mtime != NULL) {
				/*先转lower case*/
				char *file_last_mtime = str_2_lower(ctime(&last_modified_time), strlen(ctime(&last_modified_time)));
#ifdef _DEBUG
				//ctime() end with '\n\0';
				printf("file_last_mtime::%.*s::\n", last_mtime_len, file_last_mtime);
				printf("reqt_last_mtime::%.*s::\n", last_mtime_len, last_mtime);
#endif	
				if(str_equal((char *)last_mtime, last_mtime_len, file_last_mtime)) {
					fd_records[sock].http_code = 304;
					dbg_printf("304 not modified!");
				}
			}
		}

		char content_type[64];
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
			/*the type of dir must be "text/html"*/
			if(fd_records[sock].http_code != DIR_CODE) {
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
			} else {
				strcpy(content_type, "text/html");
			}
		}

		int header_length = 0;

		if(fd_records[sock].http_code == 200 || fd_records[sock].http_code == DIR_CODE) {
			
			if(fd_records[sock].http_code == 200) {
				/*because ctime() retuan a string end with '\n', so no more '\n' is add below*/
				header_length = sprintf(fd_records[sock].buf, \
					"%sContent-Type: %s\r\nContent-Length: %d\r\nLast-Modified:%sCache-Control: max-age=%d\r\n", \
					header_200_ok, content_type, (int)filestat.st_size, ctime(&last_modified_time), conf.cache_control_max_age);

			} else {  /*folder*/
				header_length = sprintf(fd_records[sock].buf, \
					"%sContent-Type: %s\r\nCache-Control: max-age=%d\r\n", \
					header_200_ok, content_type, conf.cache_control_max_age);
			}
		}	
		else if(fd_records[sock].http_code == 404) {
			header_length = sprintf(fd_records[sock].buf, \
				"%sContent-Type: %s\r\nContent-Length: %d\r\n", \
				header_404_not_found, content_type, (int)filestat.st_size);
		}
		else if(fd_records[sock].http_code == 304) {
			header_length = sprintf(fd_records[sock].buf, "%s\r\n", header_304_not_modified);
		}
		if(fd_records[sock].keep_alive && fd_records[sock].http_code != 304) {
			header_length += sprintf(fd_records[sock].buf+header_length, "%s\r\n\r\n", "Connection: Keep-Alive");
		} else {
			header_length += sprintf(fd_records[sock].buf+header_length, "%s\r\n\r\n", "Connection: Close");
		}
		fd_records[sock].buf[header_length] = '\0';

		int ret;
		//////////////////////////////////////////////////////////////
		// stop the read
		// thus, not support the http pipeline... 
		/////////////////////////////////////////////////////////////
		ret = ev_stop(loop, sock, EV_READ);
		if(ret == -1) {
			safe_close(loop, sock);
			return NULL;
		}

		ret = ev_register(loop, sock, EV_WRITE, write_http_header);
		if(ret == -1) {
			if(conf.log_enable) {
				log_error("ev register err in read_http()\n");
			} else {
				fprintf(stderr,"ev register err in read_http()\n");
			}
			delete_timer(loop, sock);
			return NULL;
		}
	}
	else {
		safe_close(loop, sock);
		return NULL;	
	}
	return NULL;
}



void *write_http_header(ev_loop_t *loop, int sockfd, EV_TYPE events){
	if(sockfd > conf.max_conn) {
		safe_close(loop, sockfd);
		return NULL;
	}

	// if(conf.use_tcp_cork) {
 //    	int on = 1;
 //    	setsockopt(sockfd, SOL_TCP, TCP_CORK, &on, sizeof(on));
 //    }
	while(1) {
		int nwrite;
		nwrite = write(sockfd, fd_records[sockfd].buf+fd_records[sockfd].write_pos, strlen(fd_records[sockfd].buf)-fd_records[sockfd].write_pos);
		if(nwrite > 0) {
			fd_records[sockfd].write_pos+=nwrite;
		}
		if(nwrite == -1) {
			if(errno != EAGAIN)
			{
				if(conf.log_enable) {
					log_error("%s\n", strerror(errno));
				} else {
					fprintf(stderr,"%s\n", strerror(errno));
				}
				safe_close(loop, sockfd);
				return NULL;
			}
			break;
		}

		if(fd_records[sockfd].write_pos == strlen(fd_records[sockfd].buf)) {
			fd_records[sockfd].write_pos = 0;
			
			if(fd_records[sockfd].http_code == 304) {
				safe_close(loop, sockfd);
				return NULL;
			}
			if(fd_records[sockfd].http_code == 2048) {
				safe_close(loop, sockfd);
				dbg_printf("==============2048===============\n");
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
					delete_timer(loop, sockfd);
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
					safe_close(loop, sockfd);
					return NULL;
				}
				int ret = ev_register(loop, sockfd, EV_WRITE, write_dir_html);
				if(ret == -1) {
					if(conf.log_enable) {
						log_error("ev register err in write_http_header2()\n");
					} else {
						fprintf(stderr,"ev register err in write_http_header2()\n");
					}
					delete_timer(loop, sockfd);
					return NULL;
				}
			}
			return NULL;
		}
	}
	return NULL;
}

void *write_dir_html(ev_loop_t *loop, int sockfd, EV_TYPE events) {
	if(sockfd > conf.max_conn) {
		safe_close(loop, sockfd);
		return NULL;
	}

	// if(conf.use_tcp_cork) {
 //    	int on = 1;
 //    	setsockopt(sockfd, SOL_TCP, TCP_CORK, &on, sizeof(on));
 //    }
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
                safe_close(loop, sockfd);
				return NULL;
			}
			break;
		}

		if(fd_records[sockfd].write_pos == strlen(fd_records[sockfd].buf)) {
			fd_records[sockfd].write_pos = 0;
			safe_close(loop, sockfd);
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
	if(sockfd > conf.max_conn) {
		safe_close(loop, sockfd);
		return NULL;
	}
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
		   		safe_close(loop, sockfd);
		   		close(ffd);

				return NULL;
			} else {
				// 写入到缓冲区已满了
				break;
			}
	    }
	    if(fd_records[sockfd].read_pos == fd_records[sockfd].total_len) {
	   		int keep_alive = fd_records[sockfd].keep_alive;
	   		ev_timer_t *timer= (ev_timer_t *)fd_records[sockfd].timer_ptr;
	   		int flag = 0;
	   		if(timer != NULL) {
	   			timer->cb = NULL;
	   			flag = 1;
	   		}
	   		/*****************************************************************
	   		 * 明显的一个需要改进的地方，不需要每次都先unregister 然后在重新注册，
	   		 * 不过改动容易出问题，先保留
	   		 *****************************************************************/
	   		ev_unregister(loop, sockfd);
	  		if(keep_alive) {
	  			ev_register(loop, sockfd, EV_READ, read_http);
	  			if(!flag) {
	  				add_timer(loop, 40, process_timeout, 0, 0, (void*)sockfd);
	  			} else {
	  				dbg_printf("reuse sockfd!");
	  				add_timer(loop, 40, process_timeout, 0, 0, (void*)sockfd);
	  			}
	   		}
	   		else {
	   			close(sockfd);
	   		}
	   		close(ffd);

	   		return NULL;
	    }
  	}
	return NULL;
}

