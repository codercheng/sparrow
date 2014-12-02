OBJS = sparrow.o thread_manage.o file.o ev_loop.o config.o async_log.o url.o min_heap.o cJSON.o mysql_encap.o conn_pool.o
CC = gcc
DEBUG = -g
CFLAGS = -Wall -c $(DEBUG)
LFLAGS = -Wall -pthread  $(DEBUG)

sparrow: $(OBJS)
	g++ $(LFLAGS) $(OBJS) -lrt -lm  -lmysqlclient -o sparrow

sparrow.o: sparrow.c sparrow.h global.h  ev_loop.h async_log.h thread_manage.h config.h file.h mime.h url.h min_heap.h cJSON.h mysql_encap.h conn_pool.h
	g++ $(CFLAGS)  sparrow.c 

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

min_heap.o: min_heap.c min_heap.h ev_loop.h
	$(CC) $(CFLAGS) min_heap.c

cJSON.o: cJSON.c cJSON.h
	$(CC) $(CFLAGS) cJSON.c
mysql_encap.o: mysql_encap.cpp mysql_encap.h lock.h
	g++ $(CFLAGS) mysql_encap.cpp
conn_pool.o: conn_pool.cpp conn_pool.h mysql_encap.h
	g++ $(CFLAGS) conn_pool.cpp

clean:
	rm *.o sparrow
