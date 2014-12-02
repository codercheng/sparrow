#include "conn_pool.h"
#include <assert.h>
#include <stdio.h>

ConnPool *ConnPool::conn_pool = NULL;

ConnPool::ConnPool(const char *hostip, const char *username, const char *password,
			 int maxSize, int initSize){
	assert(maxSize >= initSize);
	assert(maxSize>0 && initSize>0);

	hostip_   = hostip;
	username_  = username;
	password_ = password;

	maxSize_  = maxSize;
	curSize   = 0;
	InitConnPool(hostip, username, password, initSize);
}

ConnPool *ConnPool::GetInstance(){
	if(conn_pool == NULL)
		conn_pool = new ConnPool(HOST, USERNAME, PASSWORD, MAX_CONNPOOL_SIZE, INIT_CONNPOOL_SIZE);
	return conn_pool;
}

void ConnPool::InitConnPool(const char *hostip, const char *username, const char *password,
			int initSize){
	MutexGuard lock(mutex);
	MysqlEncap *conn;
	try{
		for(int i=0; i<initSize; i++){
			conn = new MysqlEncap;
			conn->Connect(hostip_.c_str(), username_.c_str(), password_.c_str());
			if(conn != NULL){
				connList.push_back(conn);
				curSize++;
			}else{
				fprintf(stderr,"InitConnPool error\n");
			}
		}
	}catch(...){
		fprintf(stderr,"InitConnPool error\n");
	}
}

MysqlEncap* ConnPool::GetOneConn(){
	MysqlEncap *conn;
	MutexGuard lock(mutex);
	if(connList.size() > 0){//注意区分connlist里面的是可用连接，而cursize表示所有建立的连接
		conn = connList.front();
		connList.pop_front();
		//curSize--;//不能--
	}else{
		if(curSize < maxSize_){
			conn = new MysqlEncap;
			conn->Connect(hostip_.c_str(), username_.c_str(), password_.c_str());
			if(conn != NULL){
				//connList.push_back(conn);//这里不能加入到队列，直接反给使用者
				curSize++;
			}else{
				fprintf(stderr,"get conn error\n");
			}
		}else{
			return NULL;
		}
	}
	return conn;
}


void ConnPool::ReleaseOneConn(MysqlEncap *conn){
	if(conn){
		MutexGuard lock(mutex);
		connList.push_back(conn);	
	}
}

ConnPool::~ConnPool(){
	DestroyConnPool();
}
//目前只保证connlist里面空闲连接被释放，剩下的分配出去的连接
//还没有去管，基于不是程序退出不会销毁线程池，所以也没关系
void ConnPool::DestroyConnPool(){
	list<MysqlEncap *>::iterator iter;
	MutexGuard lock(mutex);
	for(iter=connList.begin(); iter!=connList.end(); iter++){
		DestroyConnection(*iter);
	}
	curSize = 0;
	connList.clear();
}

void ConnPool::DestroyConnection(MysqlEncap *conn){
	if(conn){
		conn->CloseConnect();
	}
	delete conn;
}

void ConnPool::ShowStatus(){
	printf("+++++++The Pool Status++++++\n");
	printf("curSize: %d\n", curSize);
	printf("free conn: %d    used conn: %d\n", connList.size(), curSize-connList.size());
	printf("++++++++++ End +++++++++++++\n");
}






