#ifndef _GLOBAL_H
#define _GLOBAL_H

#include "async_log.h"

/**
 * log info
 */
#define LOG_TIME_OUT_SECOND 60
#define MIN_LOG_LEVEL LOG_INFO

/**
 * httpd
 */
#define LISTEN_PORT 6789

#define MAX_EVENT 1000
#define USE_EPOLLET 1

#define WORKER_THREAD_NUM 1


//#define NO_SOCK_FD -1
#define NO_FILE_FD -1
#define WORKING_DIR "./"


#define TCP_RECV_BUF 16*1024
#define TCP_SEND_BUF 16*1024
//buf size must be large enough to contain the tcp recv/send buf in ET model
#define MAXBUFSIZE 16*10224

#define header_200_start "HTTP/1.1 200 OK\r\nServer: chengshuguang/0.1\r\n" \
   						 "Content-Type: text/html\r\nAccept-Charset: utf-8\r\nAccept-Language: en-US,en;q=0.5,zh-CN;q=0.5\r\nConnection: Close\r\n"
#define _DEBUG_


#endif



