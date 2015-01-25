#ifndef _GLOBAL_H
#define _GLOBAL_H

#define NO_FILE_FD -1
#define TCP_RECV_BUF 16*1024
#define TCP_SEND_BUF 16*1024

//buf size must be large enough to contain the tcp recv/send buf in ET model
#define MAXBUFSIZE 64*1024


#define header_200_ok "HTTP/1.1 200 OK\r\nServer: Sparrow/v0.1\r\n" \
	"Accept-Charset: utf-8\r\nAccept-Language: en-US,en;q=0.5,zh-CN;q=0.5\r\n"
#define header_404_not_found "HTTP/1.1 404 Not Found\r\nServer: Sparrow/v0.1\r\n" \
	"Accept-Charset: utf-8\r\nAccept-Language: en-US,en;q=0.5,zh-CN;q=0.5\r\n"
#define header_304_not_modified "HTTP/1.1 304 Not Modified\r\n"

#define DIR_FIRST_PART "dir.part1"
#define DIR_SECOND_PART "dir.part2"
//
//for debug
#define _DEBUG

#endif



