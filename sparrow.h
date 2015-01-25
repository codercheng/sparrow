#ifndef _SPARROW_H
#define _SPARROW_H

#ifdef __cplusplus
extern "C" 
{
#endif


#include "ev_loop.h"

#define DIR_CODE 1024

void *accept_sock(ev_loop_t *loop, int sock, EV_TYPE events);
void *read_http(ev_loop_t *loop, int sock, EV_TYPE events);
void *write_http_header(ev_loop_t *loop, int sockfd, EV_TYPE events);
void *write_http_body(ev_loop_t *loop, int sockfd, EV_TYPE events);
void *process_dir(ev_loop_t *loop, int sockfd, EV_TYPE events);
int process_dir_html(char *path, int sockfd);
void *write_dir_html(ev_loop_t *loop, int sockfd, EV_TYPE events);

#ifdef __cplusplus
}
#endif

#endif