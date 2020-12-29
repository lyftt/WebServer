#ifndef __REDIS_CONNECTION_POOL_H__
#define __REDIS_CONNECTION_POOL_H__

#include <string>
#include <vector>
#include "hiredis.h"
#include "locker.h"

using namespace std;

enum USE_STATUS
{
	UNUSED,
	USED
};

struct redis_pool_obj
{
	redisContext* conn;
	USE_STATUS conn_use_status;

	redis_pool_obj(redisContext* con):conn(con),conn_use_status(UNUSED)
	{

	}

	~redis_pool_obj()
	{

	}
};

class RedisConnectionPool
{
public:
	RedisConnectionPool();
	~RedisConnectionPool();

	/*初始化*/
	bool init(string url,int port,int max_conns);

	/*销毁整个连接池*/
	bool destory_pool();

	/*从池子中获取一个连接*/
	redisContext* get_connection();

	/*释放一个连接，重新放回连接池*/
	bool release_connection(redisContext* conn);

	/*单例*/
	static RedisConnectionPool* m_instance;    //单例redis连接池对象
	static RedisConnectionPool* GetInstance(); //获取连接池
	static void RevInstance();

private: 
	string m_url;          //redis服务器地址
	string m_port_s;       //redis服务器端口号
	int m_port;            //端口号，整型

	int m_max_conns;       //最大的连接数量
	int m_free_conns;      //剩余连接数量
	int m_cur_conns;       //已经使用连接数量

	vector<redis_pool_obj> m_redis_pool;  //池

	Sem m_sem;              //信号量
	static Locker m_lock;   //锁
};


class RedisConnRAII
{
public:
	RedisConnRAII(redisContext** conn, RedisConnectionPool* pool);
	~RedisConnRAII();

private:
	redisContext* m_conn;
	RedisConnectionPool* m_pool;

};

#endif