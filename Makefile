OBJS = sparrow.o thread_manage.o file.o ev_loop.o config.o async_log.o url.o
CC = gcc
DEBUG = -g
CFLAGS = -Wall -c $(DEBUG)
LFLAGS = -Wall -pthread  $(DEBUG)

sparrow: $(OBJS)
	$(CC) $(LFLAGS) $(OBJS) -lrt -o sparrow

sparrow.o: sparrow.c sparrow.h global.h  ev_loop.h async_log.h thread_manage.h config.h file.h mime.h url.h
	$(CC) $(CFLAGS)  sparrow.c

thread_manage.o: thread_manage.c thread_manage.h sparrow.h global.h async_log.h ev_loop.h  config.h file.h mime.h
	$(CC) $(CFLAGS) -pthread thread_manage.c

ev_loop.o: ev_loop.c ev_loop.h async_log.h
	$(CC) $(CFLAGS) ev_loop.c

file.o: file.c file.h async_log.h config.h global.h
	$(CC) $(CFLAGS) file.c

async_log.o: async_log.c async_log.h
	$(CC) $(CFLAGS) -pthread  async_log.c -lrt

config.o: config.c config.h
	$(CC) $(CFLAGS) config.c

url.o: url.c url.h
	$(CC) $(CFLAGS) url.c

clean:
	rm *.o sparrow
