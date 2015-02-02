#ifndef _MIN_HEAP_H
#define _MIN_HEAP_H


#ifdef __cplusplus
extern "C" 
{
#endif


#include <time.h>
#include <stdint.h>
#include "ev_loop.h"
struct ev_timer_t;
typedef void (* cb_timer_t)(ev_loop_t *loop, struct ev_timer_t *timer);


typedef struct ev_timer_t{
  uint8_t groupid;
  uint8_t repeat;
  double timeout;
  struct timespec ts;
  cb_timer_t cb;
  union {
    int fd;
    void *ptr;
  };
  //struct ev_timer_t *next;
} ev_timer_t;

extern void timer_heap_init(ev_loop_t *loop, int capacity);
extern void add_timer(ev_loop_t *loop, double timeout, cb_timer_t cb, uint8_t repeat, uint8_t groupid, void *ptr);
extern struct timespec tick(ev_loop_t *loop);
extern void* check_timer(ev_loop_t *loop, int tfd, EV_TYPE events);
extern void delete_timer(ev_loop_t *loop, int sockfd);
//ev_timer_t *freelist;

#ifdef __cplusplus
}
#endif

#endif
