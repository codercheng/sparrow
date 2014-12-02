/**************************************************
*  该类主要是对mutex进行了RAII封装，以后还会封装condition,
*  目的当然是为了便于编程使用啊
*  用法：
*  MutexLock mutex;//定义一个mutex
*
*  void foo(){
		...
*  		MutexGuard lock(mutex);
*   	...
*   	do something...
*		...
*  }
*  lock 只在它的作用域内有效
**************************************************/

#ifndef LOCK_H_
#define LOCK_H_

#include <pthread.h>

/************************************************
* mutex api的简单封装
*************************************************/
class MutexLock{
public:
	MutexLock(){
		pthread_mutex_init(&mutex_, NULL);
	}
	~MutexLock(){
		pthread_mutex_destroy(&mutex_);
	}

	void Lock(){
		pthread_mutex_lock(&mutex_);
	}
	void UnLock(){
		pthread_mutex_unlock(&mutex_);
	}

private:
	pthread_mutex_t mutex_;

private:
	//禁止copy
	MutexLock(const MutexLock &);
	MutexLock &operator=(const MutexLock &);
};

/************************************************
* 只是简单的对mutex做RAII封装
* use:
* void foo(){
*    MutexGuard lock(mutex);
*    ...
*    do something...
*    ...
* 	}
*************************************************/
class MutexGuard{
public:
	//explicit 防止隐式转换
	explicit MutexGuard(MutexLock &mutex):mutex_(mutex){//引用只能用
		//mutex_ = mutex;
		mutex_.Lock();
	}
	~MutexGuard(){
		mutex_.UnLock();
	}
private:
	MutexLock &mutex_;//由于mutexLock不允许拷贝，所以用引用

private:
	//禁止copy
	MutexGuard(const MutexGuard &);
	MutexGuard &operator=(const MutexGuard &);
};
// 见陈硕的muduo网络库实现
#define MutexGuard(x) do{\
		fprintf(stderr, "Error:line:%d, function:%s(), file:%s\nthis should be used like this:\nMutexGuard lock(your_mutex);\n",\
		__LINE__, __FUNCTION__, __FILE__);\
	}while(0);
	
#endif
