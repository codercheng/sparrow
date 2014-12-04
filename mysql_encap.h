#ifndef MYSQL_ENCAP_H_
#define MYSQL_ENCAP_H_

#include <string>
#include <string.h>
#include <map>
#include <mysql/mysql.h>

using namespace std;

class MysqlEncap{

public:
	MysqlEncap();
	~MysqlEncap();
	
	bool Connect(const char *ip, const char *username, const
			char *password);
	void CloseConnect();
	
	
	bool ExecuteQuery(const char *sql);
	bool Execute(const char *sql);
	bool StartTransaction();
	//修改为：当返回值0的时候，是ROLLBACK，1--COMMIT
	bool EndTransaction();
	
	//return the query result count, that is, how many rows
	int GetQueryResultCount();
	
	char *GetField(const char *fieldname);//根据列的名字查找当前行，得到该列的值
	char *GetField(int filednum);//根据列的序号查找当前行，得到该列的值
	char **FetchRow();//拿出一行结果

	void EscapeString(char *to, char *from);
	
private:
	bool ReConnect();
	void FreeResult();//释放上一次查询的结果
	
private:

	bool   isConnected;//判断是否连接
	bool   bCommit;//当用到事务时，该值用来选择是commit还是rollback
	int    fieldcnt;//查询结果有几列, column not rows
	map<string, int> mapFiledToIndex;//查询结果列名字到序号的映射：id->0; name->1
	
	MYSQL      sql_conn;//数据库连接
	MYSQL_RES *sql_result;//查询结果
	MYSQL_ROW  sql_row;//结果中的当前行，FetchRow();
	
	//保存连接信息，保证重连
	string hostip_;
	string username_;
	string password_;

};

#endif
