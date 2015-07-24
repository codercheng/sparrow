BIN := sparrow
OBJS := sparrow.o thread_manage.o file.o ev_loop.o config.o async_log.o url.o min_heap.o cJSON.o picohttpparser.o
CC := gcc
DEBUG := -g
CFLAGS := -Wall -c $(DEBUG)
LFLAGS :=  -pthread -lrt -lm

sparrow: $(OBJS)
	$(CC) $^ $(LFLAGS)  -o $@

sparrow.o: sparrow.c sparrow.h global.h  ev_loop.h async_log.h thread_manage.h config.h file.h mime.h url.h min_heap.h cJSON.h picohttpparser.h
	$(CC) $(CFLAGS)  $<

thread_manage.o: thread_manage.c thread_manage.h sparrow.h global.h async_log.h ev_loop.h  config.h file.h mime.h
	$(CC) $(CFLAGS) $<

ev_loop.o: ev_loop.c ev_loop.h async_log.h
	$(CC) $(CFLAGS) $<

file.o: file.c file.h async_log.h config.h global.h
	$(CC) $(CFLAGS) $<

async_log.o: async_log.c async_log.h
	$(CC) $(CFLAGS) $< 

config.o: config.c config.h
	$(CC) $(CFLAGS) $<

url.o: url.c url.h
	$(CC) $(CFLAGS) $<

min_heap.o: min_heap.c min_heap.h ev_loop.h
	$(CC) $(CFLAGS) $<

cJSON.o: cJSON.c cJSON.h
	$(CC) $(CFLAGS) $<
picohttpparser.o: picohttpparser.c picohttpparser.h
	$(CC) $(CFLAGS) $<

clean:
	rm $(OBJS) $(BIN) -f
