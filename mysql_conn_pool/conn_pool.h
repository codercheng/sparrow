#ifndef CONN_POOL_H_
#define CONN_POOL_H_

#include "mysql_encap.h"
#include "lock.h"
#include <list>
#include <string>

#define HOST 		"localhost"
#define USERNAME 	"root"
#define PASSWORD 	"110315"

#define MAX_CONNPOOL_SIZE 	4
#define INIT_CONNPOOL_SIZE 	2

class ConnPool{
public:
	static ConnPool *GetInstance();
	MysqlEncap *GetOneConn();
	void ReleaseOneConn(MysqlEncap *);
	void ShowStatus();
private:
	int curSize;//目前已有的连接，包括connlist中空闲的和已经分配出去的
	int maxSize_;

	string hostip_;
	string username_;
	string password_;

	list<MysqlEncap *> connList;//空闲的conn
	MutexLock mutex;

	static ConnPool *conn_pool;//singleton

	ConnPool(const char *hostip, const char *username, const char *password,
			int maxSize, int initSize);
	~ConnPool();
	void InitConnPool(const char *hostip, const char *username, const char *password,
			int initSize);
	void DestroyConnection(MysqlEncap *sql_conn);
	void DestroyConnPool();

private:
	//禁止copy
	ConnPool(const ConnPool &);
	ConnPool &operator=(const ConnPool &);
};

#endif
