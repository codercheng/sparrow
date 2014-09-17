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
#define LISTEN_PORT 6868

#define MAX_EVENT 1000
#define USE_EPOLLET 1

#define WORKER_THREAD_NUM 1


//#define NO_SOCK_FD -1
#define NO_FILE_FD -1
#define WORKING_DIR "./tmp/"


#define TCP_RECV_BUF 16*1024
#define TCP_SEND_BUF 16*1024
//buf size must be large enough to contain the tcp recv/send buf in ET model
#define MAXBUFSIZE 32*1024


#define CacheControl_MaxAge 60

#define header_200_ok "HTTP/1.1 200 OK\nServer: bitchttpd/v0.1\n" \
   						 "Accept-Charset: utf-8\nAccept-Language: en-US,en;q=0.5,zh-CN;q=0.5\n"
#define header_404_not_found "HTTP/1.1 404 Not Found\nServer: bitchttpd/v0.1\n" \
   						 "Accept-Charset: utf-8\nAccept-Language: en-US,en;q=0.5,zh-CN;q=0.5\n"
#define header_304_not_modified "HTTP/1.1 304 Not Modified\n Connection: Close\n"
#define _DEBUG_


#define DIR_FIRST_PART "dir.part1"
#define DIR_SECOND_PART "dir.part2"


#endif



