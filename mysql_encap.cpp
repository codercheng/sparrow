#include "mysql_encap.h"
#include <stdio.h>
#include <string.h>
#include <map>
#include <assert.h>


using namespace std;

/*构造函数*/
MysqlEncap::MysqlEncap(){
	isConnected = false;
	sql_result = NULL;
	mysql_init(&sql_conn);
}
/*析构函数*/
MysqlEncap::~MysqlEncap(){
	FreeResult();
	CloseConnect();
}
/****************************************************
* 建立数据库连接
* ip: 数据库主机ip
* username: 登录mysql用户名
* password: 密码
****************************************************/
bool MysqlEncap::Connect(const char *ip, const char *username, const
		char *password){
	hostip_ = ip;
	username_ = username;
	password_ = password;
	if(isConnected){
		return true;
	}
	if(mysql_real_connect(&sql_conn, ip, username, password,
				NULL, 0, NULL, 0) == NULL){
		fprintf(stderr, "%s\n", mysql_error(&sql_conn));
		return false;
	}
	isConnected = true;
	printf("connect establish [user:%s]\n",username);
	return true;
}

/************************************************
* 关闭连接
************************************************/
void MysqlEncap::CloseConnect(){
	mysql_close(&sql_conn);
	isConnected = false;
}

/************************************************
* 重新连接
************************************************/
bool MysqlEncap::ReConnect(){
	CloseConnect();
	if(mysql_real_connect(&sql_conn, hostip_.c_str(), username_.c_str(), password_.c_str(),
				 NULL, 0, NULL, 0) == NULL){
		fprintf(stderr,"Reconnect:%s\n", mysql_error(&sql_conn));
		return false;			
	}
	isConnected = true;
	return true;
}
/************************************************
* 检查连接，如果连接断开则重连
************************************************/


/************************************************
* 查询语句
************************************************/
bool MysqlEncap::ExecuteQuery(const char *sql){
#ifdef _DEBUG
	printf("sql:%s\n",sql);
#endif
	assert(sql != NULL);
	if(sql == NULL)
		return true;
	if(!isConnected){
		fprintf(stderr,"connection was not established\n");
		bCommit = false;//如果有事务，出错不能提交
	}
	try{
		if(mysql_real_query(&sql_conn, sql, strlen(sql)) != 0){
			fprintf(stderr, "select query error\n");
			bCommit = false;
			return false;
		}
		FreeResult();
		sql_result = mysql_store_result(&sql_conn);
	} catch(...){
		ReConnect();//这种情况是在，执行语句没有出错的情况下，抛出的异常，另外isConnected = true
		isConnected = true;
		bCommit = false;//如果有事务，出错不能提交
		return false;
	}
	fieldcnt = mysql_num_fields(sql_result);
	mapFiledToIndex.clear();
	MYSQL_FIELD *fields = mysql_fetch_fields(sql_result);
	for(int i=0; i<fieldcnt; i++){
		mapFiledToIndex[fields[i].name] = i;
	}
	return true;
}
bool MysqlEncap::Execute(const char *sql){
#ifdef _DEBUG
	printf("sql:%s\n",sql);
#endif
	if(sql == NULL)
		return true;
	if(!isConnected){
		fprintf(stderr,"connection was not established\n");
		bCommit = false;//如果有事务，出错不能提交
		return false;
	}
	try{
		if(mysql_real_query(&sql_conn, sql, strlen(sql)) != 0){
			fprintf(stderr, "modify query error\n");
			bCommit = false;
			return false;
		}
		FreeResult();
		sql_result = mysql_store_result(&sql_conn);

	} catch(...){
		ReConnect();//这种情况是在，执行语句没有出错的情况下，抛出的异常，另外isConnected = true
		isConnected = true;
		bCommit = false;//如果有事务，出错不能提交
		return false;
	}
	return true;
}



bool MysqlEncap::StartTransaction(){
	bCommit = true;
	return Execute("START TRANSACTION;");
}
//bool MysqlEncap::EndTransaction(){
//	if(bCommit){
//		return Execute("COMMIT;");
//	}
//	else{
//		return Execute("ROLLBACK;");
//	}
//}
bool MysqlEncap::EndTransaction(){
	if(bCommit){
		Execute("COMMIT;");
		return 1;
	}
	else{
		Execute("ROLLBACK;");
		return 0;
	}
}

//return the query result count, that is, how many rows
int MysqlEncap::GetQueryResultCount() {
	return mysql_num_rows(sql_result);
}

/************************************************
* 释放上一次查询的结果
************************************************/
void MysqlEncap::FreeResult(){
	if(sql_result != NULL){
		mysql_free_result(sql_result);
		sql_result = NULL;
	}
}

/************************************************
* 拿到结果中的一行
************************************************/
char** MysqlEncap::FetchRow(){
	if(sql_result == NULL)
		return NULL;
	sql_row = mysql_fetch_row(sql_result);
	return sql_row;	
}

/************************************************
* 根据列的名字查找当前行，得到该列的值
************************************************/
char *MysqlEncap::GetField(const char *fieldname){
	return GetField(mapFiledToIndex[fieldname]);
}

/************************************************
* 根据列的序号查找当前行，得到该列的值
************************************************/
char *MysqlEncap::GetField(int fieldnum){
	if(fieldnum >= fieldcnt){
		fprintf(stderr,"the index is out of bound");
		return NULL;
	}
	return sql_row[fieldnum];
}

/*************************************************
 * 转义字符串,避免特殊字符
 * make sure sizeof(to) >= 2*strlen(from)+1
 *************************************************/
void MysqlEncap::EscapeString(char *dest, char *source) {
	mysql_real_escape_string(&sql_conn, dest, source, strlen(source));
}










