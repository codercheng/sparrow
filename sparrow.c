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

#include "conn_pool.h"
#include "mysql_encap.h"

char *work_dir;

int listen_sock;
ev_loop_t * listen_loop = NULL;

char dir_first_part[1024];
char dir_second_part[512];

//db连接池
ConnPool* conn_pool;

unsigned long long int round_robin_num = 0;

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

	printf("listen:%d\n", listen_sock);
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
	
	conn_pool = ConnPool::GetInstance();

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

static 
void process_timeout(ev_loop_t *loop, ev_timer_t *timer) {
	time_t t;
	t = time(NULL);
	printf("11111111------hello:%ld, i am %d\n", t, timer->fd);
	if(fd_records[timer->fd].active) {
		printf("timeout ev_unregister\n");
		ev_unregister(loop, timer->fd);
	}
	close(timer->fd);
}
static 
void process_timeout2(ev_loop_t *loop, ev_timer_t *timer) {

	int sock = timer->fd;

	cJSON *root, *obj;
	char *out;
	root = cJSON_CreateArray();
	
	obj = cJSON_CreateObject();
	cJSON_AddItemToArray(root, obj);
	cJSON_AddStringToObject(obj, "id", "timeout");
	cJSON_AddStringToObject(obj, "body", "pull timeout");
	out = cJSON_Print(root);
	cJSON_Delete(root);


	ev_timer_t * timer2 = (ev_timer_t *)(fd_records[sock].timer_ptr);
	if(timer2 != NULL) {
		timer2->cb = NULL;
	}
	int buf_len = 0;
	if(fd_records[sock].active) {
		
		buf_len = sprintf(fd_records[sock].buf, "livechat(%s)", out);
		fd_records[sock].buf[buf_len] = '\0';
		fd_records[sock].http_code = 2048;//push
		ev_register(loop, sock, EV_WRITE, write_http_header);
	}
	free(out);
}

void *accept_sock(ev_loop_t *loop, int sock, EV_TYPE events) {
	if(sock > conf.max_conn) {
		ev_unregister(loop, sock);
		close(sock);
		return NULL;
	}
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
			printf("ip:%s, conn_fd:%d\n", inet_ntoa(client_sock.sin_addr),conn_fd);
		}
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
	    printf("register fd:%d, EV_READ\n", conn_fd);
		int ret = ev_register(ev_loop_queue[0], conn_fd, EV_READ, read_http);
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
		ev_unregister(loop, sock);
		close(sock);
		return NULL;
	}

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
					log_error("read http err, %s\n", strerror(errno));
				} else {
					fprintf(stderr, "read http err, %s\n", strerror(errno));
				}
				ev_unregister(loop, sock);
				close(sock);
				return NULL;
			} else {
				break;//read complete
			}
		} else if(nread == 0) {
			//client quit
			//printf("client quit\n");
			ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
			if(timer != NULL) {
				timer->cb = NULL;
			//	printf("set cb = null\n");
			}
			ev_unregister(loop, sock);
			close(sock);
			return NULL;
		}
	}

	int header_length = fd_records[sock].read_pos;
	fd_records[sock].buf[header_length] = '\0';

	read_complete =(strstr(buf, "\n\n") != 0) ||(strstr(buf, "\r\n\r\n") != 0);

	if(read_complete) {
		if(strncmp(str_2_lower(buf, 3),"get", 3)) {
			ev_unregister(loop, sock);
			close(sock);
			return NULL;
		}
		char *path_end = strchr(buf+4, ' ');
		int len = path_end - buf - 4 -1;

		char path[1024+1];
		memset(path, 0, sizeof(path));
		if(len > 1024)
				len = 1024;
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
		char filename[1024 + 1 + strlen(work_dir)];//full path
		strncpy(filename, prefix, strlen(prefix));
		strncpy(filename+strlen(prefix), path, strlen(path)+1);

		//**************************************************************************
		// Dynamic service entry
		//**************************************************************************
		//printf("path:%s-\n", path);
		
		//stop the read
		int s_ret;
		s_ret = ev_stop(loop, sock, EV_READ);
		if(s_ret == -1) {
			ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
			if(timer != NULL) {
				timer->cb = NULL;
			}
			ev_unregister(loop, sock);
			close(sock);
			return NULL;
		}

		if(strncmp(path, "livechat", 8)==0) {
			char sql[1024];
			memset(sql, 0, sizeof(sql));

			char *p = strstr(path, "lastId=");
			if(p==NULL || p=='\0') {
				ev_unregister(loop, sock);
				close(sock);
				return NULL;
			}
			p+=7;
			char *p2 = p;
			while(isdigit(*p2)) {
				p2++;
			}
			if(p2 != NULL) {
				*p2 = '\0';
			}
			if(strlen(p) > 1024) {
				p[1024] = '\0';
			}


			long long int last_mid = atoll(p);

			//printf("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&:last_mid:%lld\n", last_mid);

			MysqlEncap *sql_conn = conn_pool->GetOneConn();
			if(last_mid == 0) {
				snprintf(sql, 1024, "select * from chatmessage.message where mid > (select max(mid) from chatmessage.message)-10;");
			} else {
				snprintf(sql, 1024, "select * from chatmessage.message where mid > %lld limit 10;", last_mid);
			}
			
			int ret;
			ret = sql_conn->ExecuteQuery(sql);
			if(!ret) {
				fprintf(stderr, "ExecuteQuery error when livechat pull come!\n");
				ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
				if(timer != NULL) {
					timer->cb = NULL;
				}
				ev_unregister(loop, sock);
				close(sock);
				return NULL;
			}
			conn_pool->ReleaseOneConn(sql_conn);

			int count = sql_conn->GetQueryResultCount();


			if(count != 0) {
				cJSON *root, *obj;
				char *out;
				root = cJSON_CreateArray();
				while(sql_conn->FetchRow()) {
					obj = cJSON_CreateObject();
					cJSON_AddItemToArray(root,obj);
					cJSON_AddStringToObject(obj, "id", sql_conn->GetField("mid"));
					cJSON_AddStringToObject(obj, "time", sql_conn->GetField("mtime"));
					cJSON_AddStringToObject(obj, "body", sql_conn->GetField("mbody"));
					//cJSON_Delete(obj);
				}
				out = cJSON_Print(root);
				//cJSON_Delete(obj);
				cJSON_Delete(root);


				ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
				if(timer != NULL) {
					timer->cb = NULL;
				}
				int buf_len = 0;
				if(fd_records[sock].active) {
					
					buf_len = sprintf(fd_records[sock].buf, "livechat(%s)", out);
					fd_records[sock].buf[buf_len] = '\0';
					fd_records[sock].http_code = 2048;//push
					ev_register(loop, sock, EV_WRITE, write_http_header);
				}
				free(out);
				return NULL;
			}


			ev_timer_t *timer= (ev_timer_t *)fd_records[sock].timer_ptr;
			if(timer == NULL) {
  				add_timer(loop, 40, process_timeout2, 0, 1, (void*)sock);
  			} else {
  				timer->cb = NULL;
  				add_timer(loop, 40, process_timeout2, 0, 1, (void*)sock);
  			}

			return NULL;
		}
		if(strncmp(path, "push", 4)==0) {
			char *p = strstr(path, "message=");
			if(p==NULL || p=='\0') {
				ev_unregister(loop, sock);
				close(sock);
				return NULL;
			}
			p+=8;
			char *p2 = strchr(p, '&');
			if(p2 != NULL) {
				*p2 = '\0';
			}
			if(strlen(p) > 1024) {
				p[1024] = '\0';
			}
			char message[1024+64];
			memset(message, 0, sizeof(message));

			int ret = 1;

			time_t t;
			t = time(NULL);


			//insert into db
			MysqlEncap *sql_conn = conn_pool->GetOneConn();

			char p_escape[1024*2+1];
			sql_conn->EscapeString(p_escape, p);

			//printf("*****StringEscape:%s\n", p_escape);

			if(sql_conn == NULL) {
				ret = 0;
			}
			char *new_mid = NULL;
			if(ret) {
				snprintf(message, 1024+64, "INSERT INTO chatmessage.message VALUES(NULL, '%ld', '%s');",\
					t, p_escape);
				ret = sql_conn->Execute(message);
				if(ret) {
					memset(message, 0 , sizeof(message));
					sprintf(message, "SELECT @@IDENTITY;");
					sql_conn->ExecuteQuery(message);

					if(sql_conn->FetchRow()) {
						new_mid = sql_conn->GetField(0);
						printf("new_message_id:%s\n", new_mid);
					}
					conn_pool->ReleaseOneConn(sql_conn);
				}
			}
			cJSON *root;
			char *out;

			if(ret) {
				memset(message, 0, sizeof(message));

				char time_now[20];
				memset(time_now, 0, sizeof(time_now));

				cJSON *obj;
			
				root = cJSON_CreateArray();
				
				obj = cJSON_CreateObject();
				cJSON_AddItemToArray(root, obj);
				cJSON_AddStringToObject(obj, "id", new_mid);
				cJSON_AddStringToObject(obj, "time", time_now);
				cJSON_AddStringToObject(obj, "body", p);

				out = cJSON_Print(root);
				cJSON_Delete(root);

				snprintf(message, 1024+64, "livechat(%s)", out);
		
				int i;
				ev_timer_t *tmp=NULL;
				int buf_len = 0;
				for(i=1; i<=loop->heap_size; i++) {
					tmp = (ev_timer_t *)(loop->heap[i]);
					if(tmp->cb != NULL && tmp->groupid == 1 && fd_records[tmp->fd].active &&!fd_records[tmp->fd].transferring) {
						//printf("push to fd:%d ...\n", tmp->fd);
						buf_len = sprintf(fd_records[tmp->fd].buf, "%s", message);
						fd_records[tmp->fd].buf[buf_len] = '\0';
						fd_records[tmp->fd].http_code = 2048;//push
						ev_register(loop, tmp->fd, EV_WRITE, write_http_header);
					}
				}
				free(out);
			}

			memset(message, 0, sizeof(message));
			root = cJSON_CreateObject();
			if(ret)
				cJSON_AddStringToObject(root, "status", "success");
			else
				cJSON_AddStringToObject(root, "status", "fail");

			out = cJSON_Print(root);
			cJSON_Delete(root);
			snprintf(message, 1024+64, "pushcall(%s)", out);
			
			//printf("********************************\n");
			//printf("%s\n", out);
			//printf("********************************\n");
			ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
			if(timer != NULL) {
				//printf("cb push-------------\n");
				timer->cb = NULL;
			}
			int buf_len = 0;
			if(fd_records[sock].active) {
				
				buf_len = sprintf(fd_records[sock].buf, "%s", message);
				fd_records[sock].buf[buf_len] = '\0';
				fd_records[sock].http_code = 2048;//push
				ev_register(loop, sock, EV_WRITE, write_http_header);
			}
			free(out);
			return NULL;
		}
		if(strncmp(path, "task_query", 10)==0) {
			char sql[1024];
			memset(sql, 0, sizeof(sql));

			char *p = strstr(path, "status=");
			if(p==NULL || p=='\0') {
				ev_unregister(loop, sock);
				close(sock);
				return NULL;
			}
			p+=7;
			int status = p[0]-'0';
			
			char *p2 = strstr(path, "time_interval=");
			if(p2==NULL || p2=='\0') {
				ev_unregister(loop, sock);
				close(sock);
				return NULL;
			}
			p2+=14;
			int time_interval = p[2]-'0';

			printf("status:%d, time_interval:%d\n", status, time_interval);

			time_t t;
			t = time(NULL);

			if(time_interval == 0) { /*1 week*/
				t -= 3600*24*7;
			} else if(time_interval == 1) { /*1 month*/
				t -= 3600*24 * 31; 
			} else if(time_interval == 2) { /*1 year*/
				t -= 3600*24*366;
			} else {
				t = 0;
			}
			// char str_time[16];
			// sprintf(str_time, "%ld", t);
			if(status != 0) {
				snprintf(sql, 1024, "select task_create_time, task_status, task_content from chatmessage.task where task_status = %d and task_create_time >= '%ld';", status, t);
			} else {
				snprintf(sql, 1024, "select task_create_time, task_status, task_content from chatmessage.task where task_create_time >= '%ld';", t);
			}
			MysqlEncap *sql_conn = conn_pool->GetOneConn();
			int ret;
			ret = sql_conn->ExecuteQuery(sql);
			if(!ret) {
				fprintf(stderr, "ExecuteQuery error when task request come!\n");
				ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
				if(timer != NULL) {
					timer->cb = NULL;
				}
				ev_unregister(loop, sock);
				close(sock);
				return NULL;
			}

			conn_pool->ReleaseOneConn(sql_conn);

			int count = sql_conn->GetQueryResultCount();

			if(count!=0) {
				cJSON *root, *obj;
				char *out;
				root = cJSON_CreateArray();
				while(sql_conn->FetchRow()) {
					obj = cJSON_CreateObject();
					cJSON_AddItemToArray(root,obj);
					cJSON_AddStringToObject(obj, "task_time", sql_conn->GetField("task_create_time"));
					cJSON_AddStringToObject(obj, "task_status", sql_conn->GetField("task_status"));
					cJSON_AddStringToObject(obj, "task_content", sql_conn->GetField("task_content"));
					//cJSON_Delete(obj);
				}
				out = cJSON_Print(root);
				//cJSON_Delete(obj);
				cJSON_Delete(root);


				ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
				if(timer != NULL) {
					timer->cb = NULL;
				}
				int buf_len = 0;
				if(fd_records[sock].active) {
					
					buf_len = sprintf(fd_records[sock].buf, "task_query_cb(%s)", out);
					fd_records[sock].buf[buf_len] = '\0';
					fd_records[sock].http_code = 2048;//task_query
					ev_register(loop, sock, EV_WRITE, write_http_header);
				}
				free(out);
				return NULL;
			}



			ev_unregister(loop, sock);
			close(sock);
			return NULL;

		}
		//**************************************************************************
		//exclude  all the other connections
		ev_unregister(loop, sock);
		close(sock);
		return NULL;
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
			//lower case
			char *time_begin = strstr(fd_records[sock].buf, "if-modified-since:");
			if(time_begin != NULL) {
				time_begin = time_begin + sizeof("if-modified-since:");
				char *time_end = strchr(time_begin, '\n');
				
				if(strncmp(str_2_lower(ctime(&last_modified_time), strlen(ctime(&last_modified_time))), time_begin, time_end - time_begin -1) == 0) {
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
		// ret = ev_stop(loop, sock, EV_READ);
		// if(ret == -1) {
		// 	ev_unregister(loop, sock);
		// 	close(sock);
		// 	return NULL;
		// }
		ret = ev_register(loop, sock, EV_WRITE, write_http_header);
		if(ret == -1) {
			//printf("ev register err in read_http()\n");
			if(conf.log_enable) {
				log_error("ev register err in read_http()\n");
			} else {
				fprintf(stderr,"ev register err in read_http()\n");
			}
			//ev_unregister(loop, sock);
			//close(sock);
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
	if(sockfd > conf.max_conn) {
		ev_unregister(loop, sockfd);
		close(sockfd);
		return NULL;
	}

	if(conf.use_tcp_cork) {
    	int on = 0;
    	setsockopt(sockfd, SOL_TCP, TCP_CORK, &on, sizeof(on));
    }
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
			if(fd_records[sockfd].http_code == 2048) {
				ev_timer_t * timer = (ev_timer_t *)(fd_records[sockfd].timer_ptr);
				if(timer != NULL) {
					timer->cb = NULL;
					//printf("set cb = null\n");
				}
				ev_unregister(loop, sockfd);
				close(sockfd);
				//printf("==============2048 transferring END===============\n");
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
					//ev_unregister(loop, sockfd);
					//close(sockfd);
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
					//ev_unregister(loop, sockfd);
					//close(sockfd);
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
		ev_unregister(loop, sockfd);
		close(sockfd);
		return NULL;
	}

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
	if(sockfd > conf.max_conn) {
		ev_unregister(loop, sockfd);
		close(sockfd);
		return NULL;
	}
	//sleep(50);
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
				//printf("w_full\n");
				break;
			}
	    }
	    if(fd_records[sockfd].read_pos == fd_records[sockfd].total_len) {
	   		int keep_alive = fd_records[sockfd].keep_alive;
	   		ev_timer_t *timer= (ev_timer_t *)fd_records[sockfd].timer_ptr;
	   		int flag = 0;
	   		if(timer != NULL) {
	   			timer->cb = NULL;
	   			printf("--------(set cb = null)-------\n");
	   			flag = 1;
	   		}
	   		ev_unregister(loop, sockfd);
	  		if(keep_alive) {
	  			ev_register(loop, sockfd, EV_READ, read_http);
	  			if(/*timer == NULL*/!flag) {
	  				add_timer(loop, 40, process_timeout, 0, 0, (void*)sockfd);
	  			} else {
	  				printf("-----=-=-=-=-=-=-=-=-=-==---resue\n");
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

