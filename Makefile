demo:
	gcc -g -Wall -lpthread sparrow.c sparrow.h global.h async_log.h async_log.c ev_loop.c ev_loop.h thread_manage.h thread_manage.c mime.h file.c file.h config.h config.c -o demo -lrt

