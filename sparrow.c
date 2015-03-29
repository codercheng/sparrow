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

	if (conf.log_enable) {
		log_init(conf.log_time_out, conf.log_level);
	}

	//for dir
	block_read(DIR_FIRST_PART, dir_first_part, sizeof(dir_first_part));
	block_read(DIR_SECOND_PART, dir_second_part, sizeof(dir_second_part));

	//mime type
	qsort(mime_type, sizeof(mime_type) / sizeof(mime_type_t), sizeof(mime_type_t), cmp);


	worker_threads_init(conf.worker_thread_num);

	signal(SIGPIPE, SIG_IGN);
	listen_sock = tcp_server(conf.listen_port);

	if (listen_sock == -1) {
		if (conf.log_enable) {
			log_error("listen err\n");
		}
		else {
			fprintf(stderr, "listen error\n");
		}
		return -1;
	}
	listen_loop = ev_create_loop(conf.max_conn, 1);
	int ret = ev_register(listen_loop, listen_sock, EV_READ, accept_sock);
	if (ret == -1) {
		if (conf.log_enable) {
			log_error("register err\n");
		}
		else {
			fprintf(stderr, "register error\n");
		}
		ev_clear(listen_sock);
		return -1;
	}

	conn_pool = ConnPool::GetInstance();

	if (conf.log_enable) {
		log_info("sparrow started successfully!\n");
	}
	else {
		fprintf(stdout, "sparrow started successfully!\n");
	}
	ev_run_loop(listen_loop);

	int i;
	for (i = 0; i < conf.worker_thread_num; i++)//等待线程全部执行完
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
	if (fd_records[timer->fd].active) {
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

static
void process_timeout2(ev_loop_t *loop, ev_timer_t *timer) {

	int sock = timer->fd;


	cJSON *root, *obj1, *obj2;
	char *out;
	root = cJSON_CreateObject();
	obj1 = cJSON_CreateArray();

	obj2 = cJSON_CreateObject();

	cJSON_AddItemToObject(root, "result", obj1);
	cJSON_AddItemToArray(obj1, obj2);
	cJSON_AddStringToObject(obj2, "id", "timeout");
	cJSON_AddStringToObject(obj2, "body", "pull timeout");
	out = cJSON_Print(root);
	cJSON_Delete(root);


	ev_timer_t * timer2 = (ev_timer_t *)(fd_records[sock].timer_ptr);
	if (timer2 != NULL) {
		timer2->cb = NULL;
	}
	int buf_len = 0;
	if (fd_records[sock].active) {

		buf_len = sprintf(fd_records[sock].buf, "livechat(%s)", out);
		fd_records[sock].buf[buf_len] = '\0';
		fd_records[sock].http_code = 2048;//push
		ev_register(loop, sock, EV_WRITE, write_http_header);
	}
	free(out);
}

void *accept_sock(ev_loop_t *loop, int sock, EV_TYPE events) {
	struct sockaddr_in client_sock;
	socklen_t len = sizeof(client_sock);
	int conn_fd;
	while ((conn_fd = accept(sock, (struct sockaddr *)&client_sock, &len)) > 0)	{
		/*limit the connection*/
		if (conn_fd >= conf.max_conn) {
			if (conf.log_enable) {
				log_warn("Too many connections come, exceeds the maximum num of the configuration!\n");
			}
			else {
				fprintf(stderr, "Warn: too many connections come, exceeds the maximum num of the configuration!\n");
			}
			close(conn_fd);
			return NULL;
		}

		setnonblocking(conn_fd);

		if (conf.log_enable) {
			log_info("Got connection from ip:%s, port:%d, conn_fd:%d\n", inet_ntoa(client_sock.sin_addr), ntohs(client_sock.sin_port), conn_fd);
		}
		else {
			printf("ip:%s, conn_fd:%d\n", inet_ntoa(client_sock.sin_addr), conn_fd);
		}
		int reuse = 1;
		setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

		// 接收缓冲区
		int nRecvBuf = TCP_RECV_BUF;
		setsockopt(conn_fd, SOL_SOCKET, SO_RCVBUF, (const char*)&nRecvBuf, sizeof(int));
		//发送缓冲区
		int nSendBuf = TCP_SEND_BUF;
		setsockopt(conn_fd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));

		// if(conf.use_tcp_cork) {
		// 	int on = 1;
		// 	setsockopt(sock, SOL_TCP, TCP_CORK, &on, sizeof(on));
		// }
		int ret = ev_register(ev_loop_queue[(round_robin_num++) % conf.worker_thread_num/*rand()%conf.worker_thread_num*/], conn_fd, EV_READ, read_http);
		if (ret == -1) {
			if (conf.log_enable) {
				log_error("register err\n");
			}
			else {
				fprintf(stderr, "ev register err in accept_sock()\n");
			}
			//ev_unregister(loop, conn_fd);
			//close(conn_fd);
			return NULL;
		}
	}
	if (-1 == conn_fd) {
		if (errno != EAGAIN && errno != ECONNABORTED   \
			&& errno != EPROTO && errno != EINTR) {//排除accpet到队列完这种返回，这只是读完了，并不是错误
			if (conf.log_enable) {
				log_error("accpet err\n");
			}
			else {
				fprintf(stderr, "accpet err\n");
			}
			return NULL;
		}
	}

	return NULL;
}
void *read_http(ev_loop_t *loop, int sock, EV_TYPE events) {
	if (sock > conf.max_conn) {
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

	while (1) {
		nread = read(sock, buf + fd_records[sock].read_pos, MAXBUFSIZE - fd_records[sock].read_pos);
		if (nread > 0) {
			read_complete = (strstr(buf + fd_records[sock].read_pos, "\n\n") != 0)
				|| (strstr(buf + fd_records[sock].read_pos, "\r\n\r\n") != 0);

			fd_records[sock].read_pos += nread;
			//判断是否读取完 \r\n\r\n
			if (read_complete) {
				break;
			}
			//问题又来了，如果对方迟迟都没有发\r\n\r\n那么岂不是要一直等下去？
			//加一个定时器
			//break;
		}
		else if (nread == -1) {
			if (errno != EAGAIN)	{
				if (conf.log_enable) {
					log_error("read http err, %s\n", strerror(errno));
				}
				else {
					fprintf(stderr, "read http err, %s\n", strerror(errno));
				}
				//是否需要处理timer呢????
				safe_close(loop, sock);
				return NULL;
			}
			else {
				//这个地方应该是返回，等下一次触发继续读
				return NULL;
				//break;//read complete
			}
		}
		else if (nread == 0) {
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
	if (pret < 0) {
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

	if (conf.log_enable) {
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

	if (read_complete) {
		//目前暂时只支持Get，排除非GET外的其他请求
		if (!str_equal((char *)method, method_len, "get")) {
			safe_close(loop, sock);
			return NULL;
		}

		//**************************************************************************
		// Dynamic service entry
		//**************************************************************************

		//stop the read
		int s_ret;
		s_ret = ev_stop(loop, sock, EV_READ);
		if (s_ret == -1) {
			ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
			if (timer != NULL) {
				timer->cb = NULL;
			}
			safe_close(loop, sock);
			return NULL;
		}

		const char *action;
		int action_len;
		KV kvs[10];
		int  kvs_num = sizeof(kvs) / sizeof(kvs[0]);

		int p_ret = parse_get_path(path, path_len, &action, &action_len, kvs, &kvs_num);
		if (p_ret == -1) {
			safe_close(loop, sock);
			return NULL;
		}
		printf("action:%.*s\n", action_len, action);


		if (strncmp(action, "/livechat", 9) == 0) {
			char sql[1024];
			memset(sql, 0, sizeof(sql));

			long long int last_mid = 0;
			int i;
			for (i = 0; i < kvs_num; i++) {
				printf("%.*s=%.*s\n", kvs[i].key_len, kvs[i].key, kvs[i].value_len, kvs[i].value);
				if (strncmp("lastId", kvs[i].key, kvs[i].key_len) == 0) {
					char tmp[32];
					sprintf(tmp, "%.*s", kvs[i].value_len, kvs[i].value);
					last_mid = atoll(tmp);
					break;
				}
			}


			MysqlEncap *sql_conn = conn_pool->GetOneConn();
			if (last_mid == 0) {
				snprintf(sql, 1024, "select * from chatmessage.message where mid > (select max(mid) from chatmessage.message)-10;");
			}
			else {
				snprintf(sql, 1024, "select * from chatmessage.message where mid > %lld limit 10;", last_mid);
			}

			int ret;
			ret = sql_conn->ExecuteQuery(sql);
			if (!ret) {
				fprintf(stderr, "ExecuteQuery error when livechat pull come!\n");
				ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
				if (timer != NULL) {
					timer->cb = NULL;
				}
				ev_unregister(loop, sock);
				close(sock);
				return NULL;
			}
			conn_pool->ReleaseOneConn(sql_conn);

			int count = sql_conn->GetQueryResultCount();


			if (count != 0) {
				cJSON *root, *obj1, *obj2;
				char *out;
				//root = cJSON_CreateArray();
				root = cJSON_CreateObject();
				obj1 = cJSON_CreateArray();
				//obj2 = cJSON_CreateObject();

				cJSON_AddItemToObject(root, "result", obj1);
				//cJSON_AddItemToArray(obj1, obj2);
	

				while (sql_conn->FetchRow()) {
					obj2 = cJSON_CreateObject();
					cJSON_AddItemToArray(obj1, obj2);
					cJSON_AddStringToObject(obj2, "id", sql_conn->GetField("mid"));
					cJSON_AddStringToObject(obj2, "time", sql_conn->GetField("mtime"));
					cJSON_AddStringToObject(obj2, "body", sql_conn->GetField("mbody"));
					//cJSON_Delete(obj);
				}
				out = cJSON_Print(root);
				//cJSON_Delete(obj);
				cJSON_Delete(root);


				ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
				if (timer != NULL) {
					timer->cb = NULL;
				}
				int buf_len = 0;
				if (fd_records[sock].active) {

					buf_len = sprintf(fd_records[sock].buf, "livechat(%s)", out);
					fd_records[sock].buf[buf_len] = '\0';
					fd_records[sock].http_code = 2048;//push
					ev_register(loop, sock, EV_WRITE, write_http_header);
				}
				free(out);
				return NULL;
			}


			ev_timer_t *timer = (ev_timer_t *)fd_records[sock].timer_ptr;
			if (timer == NULL) {
				add_timer(loop, 40, process_timeout2, 0, 1, (void*)sock);
			}
			else {
				timer->cb = NULL;
				add_timer(loop, 40, process_timeout2, 0, 1, (void*)sock);
			}

			return NULL;
		}

		if (strncmp(action, "/push", 5) == 0) {
			char message[1024 * 2 + 128];
			memset(message, 0, sizeof(message));

			int hasMessage = 0;
			int i;
			for (i = 0; i < kvs_num; i++) {
				printf("%.*s=%.*s\n", kvs[i].key_len, kvs[i].key, kvs[i].value_len, kvs[i].value);
				if (strncmp("message", kvs[i].key, kvs[i].key_len) == 0) {
					strncpy(message, kvs[i].value, kvs[i].value_len < 1024 ? kvs[i].value_len : 1024);
					hasMessage = 1;
				}
			}
			if (!hasMessage) {
				safe_close(loop, sock);
				return NULL;
			}
			//after decoding,  the len is less than before
			url_decode(message, strlen(message));
			//a copy of text body
			char body[1024];
			memset(body, 0, sizeof(body));
			strncpy(body, message, 1023);

			int ret = 1;

			time_t t;
			t = time(NULL);

			//url_decode(p, strlen(p));

			//insert into db
			MysqlEncap *sql_conn = conn_pool->GetOneConn();


			char p_escape[1024 * 2 + 1];
			if (sql_conn != NULL)
				sql_conn->EscapeString(p_escape, message);

			//printf("*****StringEscape:%s\n", p_escape);

			if (sql_conn == NULL) {
				ret = 0;
			}
			char *new_mid = NULL;
			if (ret) {
				snprintf(message, sizeof(message), "INSERT INTO chatmessage.message VALUES(NULL, '%ld', '%s');", \
					t, p_escape);
				ret = sql_conn->Execute(message);
				if (ret) {
					memset(message, 0, sizeof(message));
					sprintf(message, "SELECT @@IDENTITY;");
					sql_conn->ExecuteQuery(message);

					if (sql_conn->FetchRow()) {
						new_mid = sql_conn->GetField(0);
						printf("new_message_id:%s\n", new_mid);
					}
					conn_pool->ReleaseOneConn(sql_conn);
				}
			}
			cJSON *root;
			char *out;

			if (ret) {
				memset(message, 0, sizeof(message));

				char time_now[20];
				memset(time_now, 0, sizeof(time_now));

				cJSON *obj;

				root = cJSON_CreateArray();

				obj = cJSON_CreateObject();
				cJSON_AddItemToArray(root, obj);
				cJSON_AddStringToObject(obj, "id", new_mid);
				cJSON_AddStringToObject(obj, "time", time_now);
				cJSON_AddStringToObject(obj, "body", body);

				out = cJSON_Print(root);
				cJSON_Delete(root);

				snprintf(message, 1024 + 64, "livechat(%s)", out);

				int i;
				ev_timer_t *tmp = NULL;
				int buf_len = 0;
				for (i = 1; i <= loop->heap_size; i++) {
					tmp = (ev_timer_t *)(loop->heap[i]);
					if (tmp->cb != NULL && tmp->groupid == 1 && fd_records[tmp->fd].active &&!fd_records[tmp->fd].transferring) {
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
			if (ret)
				cJSON_AddStringToObject(root, "status", "success");
			else
				cJSON_AddStringToObject(root, "status", "fail");

			out = cJSON_Print(root);
			cJSON_Delete(root);
			snprintf(message, 1024 + 64, "pushcall(%s)", out);

			//printf("********************************\n");
			//printf("%s\n", out);
			//printf("********************************\n");
			ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
			if (timer != NULL) {
				//printf("cb push-------------\n");
				timer->cb = NULL;
			}
			int buf_len = 0;
			if (fd_records[sock].active) {

				buf_len = sprintf(fd_records[sock].buf, "%s", message);
				fd_records[sock].buf[buf_len] = '\0';
				fd_records[sock].http_code = 2048;//push
				ev_register(loop, sock, EV_WRITE, write_http_header);
			}
			free(out);
			return NULL;
		}

		if (strncmp(action, "/create_new_task", 16) == 0) {
			char message[1024 * 2 + 128];
			memset(message, 0, sizeof(message));


			int i;
			for (i = 0; i < kvs_num; i++) {
				printf("%.*s=%.*s\n", kvs[i].key_len, kvs[i].key, kvs[i].value_len, kvs[i].value);
				if (strncmp("message", kvs[i].key, kvs[i].key_len) == 0) {
					strncpy(message, kvs[i].value, kvs[i].value_len < 1024 ? kvs[i].value_len : 1024);
				}
			}

			//after decoding,  the len is less than before
			url_decode(message, strlen(message));

			int ret = 1;

			time_t t;
			t = time(NULL);
			//insert into db
			MysqlEncap *sql_conn = conn_pool->GetOneConn();

			char p_escape[1024 * 2 + 1];
			if (sql_conn != NULL)
				sql_conn->EscapeString(p_escape, message);

			if (sql_conn == NULL) {
				ret = 0;
			}

			if (ret) {
				snprintf(message, sizeof(message), "INSERT INTO chatmessage.task VALUES(NULL, 'simon', '%ld', NULL, NULL, 1, '%s');", \
					t, p_escape);
				ret = sql_conn->Execute(message);
				conn_pool->ReleaseOneConn(sql_conn);
			}
			cJSON *root;
			char *out;

			memset(message, 0, sizeof(message));
			root = cJSON_CreateObject();
			if (ret)
				cJSON_AddStringToObject(root, "status", "success");
			else
				cJSON_AddStringToObject(root, "status", "fail");

			out = cJSON_Print(root);
			cJSON_Delete(root);
			snprintf(message, 1024 + 64, "create_task_cb(%s)", out);

			ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
			if (timer != NULL) {
				timer->cb = NULL;
			}
			int buf_len = 0;
			if (fd_records[sock].active) {

				buf_len = sprintf(fd_records[sock].buf, "%s", message);
				fd_records[sock].buf[buf_len] = '\0';
				fd_records[sock].http_code = 2048;//push
				ev_register(loop, sock, EV_WRITE, write_http_header);
			}
			free(out);
			return NULL;
		}
		if (strncmp(action, "/task_query", 11) == 0) {
			char sql[1024];
			memset(sql, 0, sizeof(sql));

			int status = 0;
			int time_interval = 0;

			int i;
			for (i = 0; i < kvs_num; i++) {
				if (strncmp("status", kvs[i].key, kvs[i].key_len) == 0) {
					status = *kvs[i].value - '0';
				}
				else if (strncmp("time_interval", kvs[i].key, kvs[i].key_len) == 0) {
					time_interval = *kvs[i].value - '0';
				}
			}

			printf("status:%d, time_interval:%d\n", status, time_interval);

			time_t t;
			t = time(NULL);

			if (time_interval == 0) { /*1 week*/
				t -= 3600 * 24 * 7;
			}
			else if (time_interval == 1) { /*1 month*/
				t -= 3600 * 24 * 31;
			}
			else if (time_interval == 2) { /*1 year*/
				t -= 3600 * 24 * 366;
			}
			else {
				t = 0;
			}
			// char str_time[16];
			// sprintf(str_time, "%ld", t);
			if (status != 0) {
				snprintf(sql, 1024, "select task_id, task_create_time, task_status, task_content from chatmessage.task where task_status = %d and task_create_time >= '%ld';", status, t);
			}
			else {
				snprintf(sql, 1024, "select task_id, task_create_time, task_status, task_content from chatmessage.task where task_status != 3 and task_create_time >= '%ld';", t);
			}
			MysqlEncap *sql_conn = conn_pool->GetOneConn();
			//int ret;
			int ret = sql_conn->ExecuteQuery(sql);
			if (!ret) {
				fprintf(stderr, "ExecuteQuery error when task request come!\n");
				ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
				if (timer != NULL) {
					timer->cb = NULL;
				}
				ev_unregister(loop, sock);
				close(sock);
				return NULL;
			}

			conn_pool->ReleaseOneConn(sql_conn);

			int count = sql_conn->GetQueryResultCount();

			if (count != 0) {
				cJSON *root, *obj1, *obj2;
				root = cJSON_CreateObject();
				obj1 = cJSON_CreateArray();

				cJSON_AddItemToObject(root, "result", obj1);
				
				char *out;
				while (sql_conn->FetchRow()) {
					obj2 = cJSON_CreateObject();
					cJSON_AddItemToArray(obj1, obj2);
					cJSON_AddStringToObject(obj2, "task_id", sql_conn->GetField("task_id"));
					cJSON_AddStringToObject(obj2, "task_time", sql_conn->GetField("task_create_time"));
					cJSON_AddStringToObject(obj2, "task_status", sql_conn->GetField("task_status"));
					cJSON_AddStringToObject(obj2, "task_content", sql_conn->GetField("task_content"));
					//cJSON_Delete(obj);
				}
				out = cJSON_Print(root);
				//cJSON_Delete(obj);
				cJSON_Delete(root);


				ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
				if (timer != NULL) {
					timer->cb = NULL;
				}
				int buf_len = 0;
				if (fd_records[sock].active) {

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
		if (strncmp(action, "/task_op", action_len) == 0) {
			char sql[1024 + 64];
			memset(sql, 0, sizeof(sql));

			long long int task_id = -1;
			int op = -1;

			char str_task_id[16];
			memset(str_task_id, 0, sizeof(str_task_id));

			int i;
			for (i = 0; i < kvs_num; i++) {
				//printf("%.*s=%.*s\n", kvs[i].key_len, kvs[i].key, kvs[i].value_len, kvs[i].value);
				if (strncmp("task_id", kvs[i].key, kvs[i].key_len) == 0) {
					strncpy(str_task_id, kvs[i].value, kvs[i].value_len);
					task_id = atoll(str_task_id);
				}
				else if (strncmp("op", kvs[i].key, kvs[i].key_len) == 0) {
					op = *kvs[i].value - '0';
				}
			}


			printf("-------|task_id:%lld, op:%d\n", task_id, op);

			int ret = 1;

			//end task
			if (op == 0) {
				snprintf(sql, 1024, "update chatmessage.task set task_status = 2 where task_id =%lld;", task_id);
			}
			else if (op == 1) {//delete task
				time_t t;
				t = time(NULL);
				snprintf(sql, 1024, "update chatmessage.task set task_status = 3, task_delete_time = '%ld' where task_id = %lld;", t, task_id);
			}
			else {
				ret = 0;
			}
			if (task_id == -1)
				ret = 0;

			MysqlEncap *sql_conn;
			if (ret)
				sql_conn = conn_pool->GetOneConn();

			if (sql_conn == NULL) {
				ret = 0;
			}

			if (ret) {
				ret = sql_conn->Execute(sql);
				conn_pool->ReleaseOneConn(sql_conn);
			}
			cJSON *root;
			char *out;

			memset(sql, 0, sizeof(sql));
			root = cJSON_CreateObject();
			if (ret)
				cJSON_AddStringToObject(root, "status", "success");
			else
				cJSON_AddStringToObject(root, "status", "fail");

			out = cJSON_Print(root);
			cJSON_Delete(root);
			snprintf(sql, 1024 + 64, "task_op_cb(%s)", out);

			ev_timer_t * timer = (ev_timer_t *)(fd_records[sock].timer_ptr);
			if (timer != NULL) {
				timer->cb = NULL;
			}
			int buf_len = 0;
			if (fd_records[sock].active) {

				buf_len = sprintf(fd_records[sock].buf, "%s", sql);
				fd_records[sock].buf[buf_len] = '\0';
				fd_records[sock].http_code = 2048;//push
				ev_register(loop, sock, EV_WRITE, write_http_header);
			}
			free(out);
			return NULL;
		}
		//**************************************************************************
		//exclude  all the other connections
		safe_close(loop, sock);
		return NULL;
		//**************************************************************************


		//the last modified time of file cached in browser side
		const char *last_mtime = NULL;
		size_t last_mtime_len = 0;

		//处理Keep-alive和modified time
		for (i = 0; i != (int)num_headers; ++i) {
			if (str_equal((char *)headers[i].name, headers[i].name_len, "connection") &&
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


		char *prefix = work_dir;
		char filename[1024 + 1 + strlen(work_dir)];//full path
		memset(filename, 0, sizeof(filename));

		if (memcmp(action, "/", action_len) == 0) {
			snprintf(filename, 512, "%s/%s", prefix, conf.def_home_page);
		}
		else {
			/*limit the filename len*/
			snprintf(filename, 512, "%s%.*s", prefix, action_len, action);
		}
#ifdef _DEBUG
		char dbg_msg[1024 + 1 + strlen(work_dir)];
		memset(dbg_msg, 0, sizeof(dbg_msg));
		snprintf(dbg_msg, 512, "prefix:%s", prefix);

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
		if (-1 == s)	{
			fd_records[sock].http_code = 404;
		}
		else if (S_ISDIR(filestat.st_mode)) {
			fd_records[sock].http_code = DIR_CODE;
		}


		if (fd_records[sock].http_code == 404) {
			memset(filename, 0, sizeof(filename));
			snprintf(filename, 512, "%s/%s", prefix, "404.html");
			lstat(filename, &filestat);
		}

		int fd = -1;
		if (fd_records[sock].http_code != DIR_CODE) {
			fd = open(filename, O_RDONLY);
			if (fd == -1) {
				if (conf.log_enable) {
					log_error("can not open file:%s\n", filename);
				}
				else {
					fprintf(stderr, "can not open file:%s\n", filename);
				}
				safe_close(loop, sock);
				return NULL;
			}

			last_modified_time = filestat.st_mtime;
			//process 304 not modified
			if (last_mtime != NULL) {
				/*先转lower case*/
				char *file_last_mtime = str_2_lower(ctime(&last_modified_time), strlen(ctime(&last_modified_time)));
#ifdef _DEBUG
				//ctime() end with '\n\0';
				printf("file_last_mtime::%.*s::\n", last_mtime_len, file_last_mtime);
				printf("reqt_last_mtime::%.*s::\n", last_mtime_len, last_mtime);
#endif	
				if (str_equal((char *)last_mtime, last_mtime_len, file_last_mtime)) {
					fd_records[sock].http_code = 304;
					dbg_printf("304 not modified!");
				}
			}
		}

		char content_type[64];
		memset(content_type, 0, sizeof(content_type));

		if (fd_records[sock].http_code != 304) {

			fd_records[sock].ffd = fd;
			fd_records[sock].read_pos = 0;

			if (fd_records[sock].http_code != DIR_CODE) {
				fd_records[sock].total_len = (int)filestat.st_size;
				setnonblocking(fd);
			}
			strcpy(fd_records[sock].path, filename);


			char *suffix = strrchr(filename + 1, '.');
			/*the type of dir must be "text/html"*/
			if (fd_records[sock].http_code != DIR_CODE) {
				if (suffix == NULL) {
					if (fd_records[sock].http_code == DIR_CODE)
						strcpy(content_type, "text/html");
					else
						strcpy(content_type, "text/plain");
				}
				else {
					int index = mime_type_binary_search(mime_type, sizeof(mime_type) / sizeof(mime_type_t), suffix + 1);
					if (index == -1) {
						strcpy(content_type, "text/plain");
					}
					else {
						strcpy(content_type, mime_type[index].l_type);
					}
				}
			}
			else {
				strcpy(content_type, "text/html");
			}
		}

		int header_length = 0;

		if (fd_records[sock].http_code == 200 || fd_records[sock].http_code == DIR_CODE) {

			if (fd_records[sock].http_code == 200) {
				/*because ctime() retuan a string end with '\n', so no more '\n' is add below*/
				header_length = sprintf(fd_records[sock].buf, \
					"%sContent-Type: %s\r\nContent-Length: %d\r\nLast-Modified:%sCache-Control: max-age=%d\r\n", \
					header_200_ok, content_type, (int)filestat.st_size, ctime(&last_modified_time), conf.cache_control_max_age);

			}
			else {  /*folder*/
				header_length = sprintf(fd_records[sock].buf, \
					"%sContent-Type: %s\r\nCache-Control: max-age=%d\r\n", \
					header_200_ok, content_type, conf.cache_control_max_age);
			}
		}
		else if (fd_records[sock].http_code == 404) {
			header_length = sprintf(fd_records[sock].buf, \
				"%sContent-Type: %s\r\nContent-Length: %d\r\n", \
				header_404_not_found, content_type, (int)filestat.st_size);
		}
		else if (fd_records[sock].http_code == 304) {
			header_length = sprintf(fd_records[sock].buf, "%s\r\n", header_304_not_modified);
		}
		if (fd_records[sock].keep_alive && fd_records[sock].http_code != 304) {
			header_length += sprintf(fd_records[sock].buf + header_length, "%s\r\n\r\n", "Connection: Keep-Alive");
		}
		else {
			header_length += sprintf(fd_records[sock].buf + header_length, "%s\r\n\r\n", "Connection: Close");
		}
		fd_records[sock].buf[header_length] = '\0';

		int ret;
		//////////////////////////////////////////////////////////////
		// stop the read
		// thus, not support the http pipeline... 
		/////////////////////////////////////////////////////////////
		ret = ev_stop(loop, sock, EV_READ);
		if (ret == -1) {
			safe_close(loop, sock);
			return NULL;
		}

		ret = ev_register(loop, sock, EV_WRITE, write_http_header);
		if (ret == -1) {
			if (conf.log_enable) {
				log_error("ev register err in read_http()\n");
			}
			else {
				fprintf(stderr, "ev register err in read_http()\n");
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
	if (sockfd > conf.max_conn) {
		safe_close(loop, sockfd);
		return NULL;
	}

	// if(conf.use_tcp_cork) {
	//    	int on = 1;
	//    	setsockopt(sockfd, SOL_TCP, TCP_CORK, &on, sizeof(on));
	//    }
	while (1) {
		int nwrite;
		nwrite = write(sockfd, fd_records[sockfd].buf + fd_records[sockfd].write_pos, strlen(fd_records[sockfd].buf) - fd_records[sockfd].write_pos);
		if (nwrite > 0) {
			fd_records[sockfd].write_pos += nwrite;
		}
		if (nwrite == -1) {
			if (errno != EAGAIN)
			{
				if (conf.log_enable) {
					log_error("%s\n", strerror(errno));
				}
				else {
					fprintf(stderr, "%s\n", strerror(errno));
				}
				safe_close(loop, sockfd);
				return NULL;
			}
			break;
		}

		if (fd_records[sockfd].write_pos == strlen(fd_records[sockfd].buf)) {
			fd_records[sockfd].write_pos = 0;

			if (fd_records[sockfd].http_code == 304) {
				safe_close(loop, sockfd);
				return NULL;
			}
			if (fd_records[sockfd].http_code == 2048) {
				safe_close(loop, sockfd);
				dbg_printf("==============2048===============\n");
				return NULL;
			}

			ev_stop(loop, sockfd, EV_WRITE);
			if (fd_records[sockfd].http_code != DIR_CODE) {
				int ret = ev_register(loop, sockfd, EV_WRITE, write_http_body);
				if (ret == -1) {
					if (conf.log_enable) {
						log_error("ev register err\n");
					}
					else {
						fprintf(stderr, "ev register err in write_http_header1()\n");
					}
					delete_timer(loop, sockfd);
					return NULL;
				}
			}
			else if (fd_records[sockfd].http_code == DIR_CODE) {

				int r = process_dir_html(fd_records[sockfd].path, sockfd);
				if (r == -1) {
					if (conf.log_enable) {
						log_error("err when making dir html\n");
					}
					else {
						fprintf(stderr, "err when making dir html\n");
					}
					safe_close(loop, sockfd);
					return NULL;
				}
				int ret = ev_register(loop, sockfd, EV_WRITE, write_dir_html);
				if (ret == -1) {
					if (conf.log_enable) {
						log_error("ev register err in write_http_header2()\n");
					}
					else {
						fprintf(stderr, "ev register err in write_http_header2()\n");
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
	if (sockfd > conf.max_conn) {
		safe_close(loop, sockfd);
		return NULL;
	}

	// if(conf.use_tcp_cork) {
	//    	int on = 1;
	//    	setsockopt(sockfd, SOL_TCP, TCP_CORK, &on, sizeof(on));
	//    }
	while (1) {
		int nwrite;
		nwrite = write(sockfd, fd_records[sockfd].buf + fd_records[sockfd].write_pos, strlen(fd_records[sockfd].buf) - fd_records[sockfd].write_pos);
		fd_records[sockfd].write_pos += nwrite;
		if (nwrite == -1) {
			if (errno != EAGAIN)
			{
				if (conf.log_enable) {
					log_error("write dir html%s\n", strerror(errno));
				}
				else {
					fprintf(stderr, "write dir html%s\n", strerror(errno));
				}
				safe_close(loop, sockfd);
				return NULL;
			}
			break;
		}

		if (fd_records[sockfd].write_pos == strlen(fd_records[sockfd].buf)) {
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
	int ret = dir_html_maker(fd_records[sockfd].buf + n, path);
	if (ret == -1)
		return -1;
	sprintf(fd_records[sockfd].buf + strlen(fd_records[sockfd].buf), "%s", dir_second_part);

	return strlen(fd_records[sockfd].buf);
}


void *write_http_body(ev_loop_t *loop, int sockfd, EV_TYPE events) {
	if (sockfd > conf.max_conn) {
		safe_close(loop, sockfd);
		return NULL;
	}
	int ffd = fd_records[sockfd].ffd;
	while (1) {
		off_t offset = fd_records[sockfd].read_pos;
		int s = sendfile(sockfd, ffd, &offset, fd_records[sockfd].total_len - fd_records[sockfd].read_pos);
		fd_records[sockfd].read_pos = offset;
		if (s == -1) {
			if (errno != EAGAIN) {
				if (conf.log_enable) {
					log_error("%s\n", strerror(errno));
				}
				else {
					fprintf(stderr, "sendfile:%s\n", strerror(errno));
				}
				safe_close(loop, sockfd);
				close(ffd);

				return NULL;
			}
			else {
				// 写入到缓冲区已满了
				break;
			}
		}
		if (fd_records[sockfd].read_pos == fd_records[sockfd].total_len) {
			int keep_alive = fd_records[sockfd].keep_alive;
			ev_timer_t *timer = (ev_timer_t *)fd_records[sockfd].timer_ptr;
			int flag = 0;
			if (timer != NULL) {
				timer->cb = NULL;
				flag = 1;
			}
			/*****************************************************************
			 * 明显的一个需要改进的地方，不需要每次都先unregister 然后在重新注册，
			 * 不过改动容易出问题，先保留
			 *****************************************************************/
			ev_unregister(loop, sockfd);
			if (keep_alive) {
				ev_register(loop, sockfd, EV_READ, read_http);
				if (!flag) {
					add_timer(loop, 40, process_timeout, 0, 0, (void*)sockfd);
				}
				else {
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

