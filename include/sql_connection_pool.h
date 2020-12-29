#ifndef __SQL_CONNECTION_H__
#define __SQL_CONNECTION_H__

#include "mysql/mysql.h"
#include <list>
#include <string>
#include <iostream>
#include "locker.h"

using namespace std;

class SqlConnectionPool
{
public:
	MYSQL* get_connection();          //获取一个数据库连接

	bool release_connection(MYSQL *conn);    //释放一个数据库连接

	bool destory_pool();     //销毁所有连接

	static SqlConnectionPool* GetInstance();    //获取单一实例
	static void RevInstance();                  //销毁

	bool init(string url,string user,string password, string database_name,int port,int max_conns);    //初始化

public:
	static SqlConnectionPool* m_instance;      //单例模式

private:
	SqlConnectionPool();
	~SqlConnectionPool();

	string m_url;              //数据库主机地址
	int m_port;                //数据库mysql服务端口号
	string m_port_s;           //字符串形式端口号
	string m_user;             //登录用户
	string m_password;         //密码
	string m_database_name;    //mysql数据库名称

	list<MYSQL*> mysql_conn_list;   //链接池
	int m_max_conns;           //最大连接数量
	int m_cur_conns;           //当前已经使用的连接的数量
	int m_free_conns;          //当前剩余的连接的数量
	Sem sem;                   //信号量
	
private:
	static Locker m_mutex;            //锁
};


class ConnectionRAII
{
public:
	ConnectionRAII(MYSQL** conn,SqlConnectionPool* pool);
	~ConnectionRAII();

private:
	MYSQL* m_conn;
	SqlConnectionPool* m_pool;
};

#endif
