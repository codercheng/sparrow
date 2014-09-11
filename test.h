#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/sendfile.h>
#include <time.h>


#define DEFAULT_PORTNUM 8080
#define MAXEVENT 5000
#define USE_TCP_CORK 1

#define SENDFILE_USED 1
#define _DEBUG_ 1

#define HANDLE_ERROR_EXIT(error_string) do{perror(error_string);exit(-1);}while(0)
#define HANDLE_ERROR(error_string) do{\
	perror(error_string);\
	info_manager.give_back_node(info);\
	return;\
	}while(0)

#define header_200_start "HTTP/1.1 200 OK\r\nServer: chengshuguang/0.1\r\n" \
   						 "Content-Type: text/html\r\nAccept-Charset: utf-8\r\nAccept-Language: en-US,en;q=0.5,zh-CN;q=0.5\r\nConnection: Close\r\n"

#define header_200_start_2 "HTTP/1.1 200 OK\r\nServer: chengshuguang/0.1\r\n" \
   						 "Content-Type: json\r\nAccept-Charset: utf-8\r\nAccept-Language: en-US,en;q=0.5,zh-CN;q=0.5\r\nConnection: Close\r\n"


void write_response_header(int sock, EV_TYPE events)
{

#ifdef USE_TCP_CORK
  int on = 0;
  setsockopt(info->sockfd, SOL_TCP, TCP_CORK, &on, sizeof(on));
#endif
	while(1)
	{
		int nwrite;
		nwrite = write(info->sockfd, info->buf+info->write_pos, strlen(info->buf)-info->write_pos);
		info->write_pos+=nwrite;
		if(nwrite == -1)
		{
			if(errno != EAGAIN)
			{
				HANDLE_ERROR("write header");
			}
			return;
		}
		if(info->write_pos == strlen(info->buf))
		{
			info->write_pos = 0;
			return;
		}
	}
}
void read_request(conn_info *info)
{
	int sock =info->sockfd;
	char *buf = info->buf;
	bool read_complete = false;

	int nread = 0;

	while(1)
	{
		nread = read(sock, buf+info->read_pos, MAXBUFSIZE - info->read_pos);
		if(nread > 0)
		{
			info->read_pos+=nread;
		}
		else if(nread == -1)
		{
			if(errno != EAGAIN)
			{
				HANDLE_ERROR("read");
			}
			else
			{
				break;//read complete
			}
		}
		else if(nread == 0)
		{
			//client quit
			printf("+++++++++client quit\n");
			info_manager.give_back_node(info);
			return;
		}
	}

	int header_length = info->read_pos;
	info->buf[header_length] = '\0';

	read_complete =(strstr(buf, "\n\n") != 0) ||(strstr(buf, "\r\n\r\n") != 0);

	if(read_complete)
	{
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
	
		if(strncmp("list", path, 4) == 0) {
			
			printf("in test code... {\n");
			//int i = rand()%1000;
			char t[1024];
			memset(t, 0, sizeof(t));
			
			// //printf("%d --> loop\n", i);
			// sprintf(t, "%s\n{\"t_name\":[{\"name\":\"chengshuguang\"}, {\"name\":\"simoncheng\"}],\
			// 				\"t_year\":[{\"year\":\"21\"},{\"year\": \"23\"}]	}", header_200_start);
			//sprintf(t, "%s\r\n", header_200_start_2);
			//write(sock, t, 1024);
			get_top_n_delayed_tasks(sock, "chengshuguang", 5);

			//write(sock, t, strlen(t));
			
			close(sock);
			
			printf("} --->>end loop\n");
			//printf("sub: %d-->%d", sub_count, sock);
			//sub_list[sub_count++] = sock;
			
			return;
		}
		if(strncmp("insert", path, 6) == 0) {
			printf("<here>\n");
			char t[1024];
			memset(t, 0, sizeof(t));
			sprintf(t, "%s\r\nthis is the return value!", header_200_start);
			write(sock, t, 1024);
			return;
		}
//		if(strncmp("pub", path, 3) == 0) {
//			
//			printf("in test code... {\n");
//			int i = rand()%1000;
//			char t[4096];
//			memset(t, 0, sizeof(t));
//			
//			printf("%d --> loop\n", i);
//			sprintf(t, "%s\nrandom pub :%d", header_200_start, i);
//			for(int i = 0; i < sub_count; i++)
//			{
//				printf("[%d] = %d\n", i, sub_list[i]);
//				write(sub_list[i], t, strlen(t));
//				close(sub_list[i]);
//			}
//			//sub_count = 0;
//			
//			
//			printf("} --->>end loop\n");
//			return;
//		}
		printf("*************************************\n");
		
		struct stat filestat;
		int s = lstat(filename, &filestat);
		if(-1 == s)
		{
			HANDLE_ERROR("not a file or a dir");
		}
		if(S_ISDIR(filestat.st_mode))
		{
			HANDLE_ERROR("Not a file");
		}

		int fd = open(filename, O_RDONLY);
		if(-1 == fd)
		{
			//info_manager.give_back_node(info);
			HANDLE_ERROR("can not open file");
		}
		info->ffd = fd;
		info->read_pos =0;
		info->total_len = (int)filestat.st_size;

		ev.data.ptr = info;
		ev.events = EPOLLOUT | EPOLLET;

		if(-1 == epoll_ctl(epfd, EPOLL_CTL_MOD, sock, &ev))
		{
			HANDLE_ERROR("epoll read");
		}

		sprintf(info->buf,"%sContent-Length: %d\r\n\r\n", header_200_start, (int)filestat.st_size);
		//printf("========html:\n%s\n---------\n",buf);
		write_response_header(info);
	}
	else
	{
		printf("not a header\n");
		info_manager.give_back_node(info);
		return;	
	}
}

void write_response(conn_info *info)
{
	int sockfd =info->sockfd;
	int ffd = info->ffd;
	while(1) 
	{
	    off_t offset = info->read_pos;
	    int s = sendfile(sockfd, ffd, &offset, info->total_len - info->read_pos);
	    info->read_pos = offset;
	    if(s == -1) 
	    {
	      if(errno != EAGAIN) 
	      {
	        HANDLE_ERROR("sendfile");
	      } 
	      else 
	      {
	        // 写入到缓冲区已满了
	        return;
	      }
	    }
	    if(info->read_pos == info->total_len) {
	      // 读写完毕
	    //printf("end\n");
	    close(ffd);
	    info_manager.give_back_node(info);	
	    return;
	    }
  	}
}