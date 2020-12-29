#include  "sql_connection_pool.h"
#include "log.h"
#include "mysql/mysql.h"
#include <string>

//静态遍历初始化
SqlConnectionPool* SqlConnectionPool::m_instance = NULL;

Locker SqlConnectionPool::m_mutex;

/*
* 
* 构造函数
* 
*/
SqlConnectionPool::SqlConnectionPool()
{
	m_cur_conns = 0;
	m_free_conns = 0;
	m_max_conns = 0;
}

/*
* 
* 析构
* 
*/
SqlConnectionPool::~SqlConnectionPool()
{
	destory_pool();    //销毁线程池
}

/*
* 
* 获取唯一实例
* 
*/
SqlConnectionPool* SqlConnectionPool::GetInstance()
{
	if (m_instance == NULL)
	{
		m_mutex.lock();
		if (m_instance == NULL)
		{
			m_instance = new (std::nothrow) SqlConnectionPool;   //如果失败，不抛出异常
		}
		m_mutex.unlock();
	}

	return m_instance;
}

/*
* 
* 消除单一实例
* 
*/
void SqlConnectionPool::RevInstance()
{
	if (m_instance)
	{
		m_mutex.lock();

		if (m_instance)
		{
			delete m_instance;
			m_instance = NULL;
		}

		m_mutex.unlock();
	}
}

/*
* 
* 初始化连接池,初始化多个MYSQL数据库连接句柄
* 
*/
bool SqlConnectionPool::init(string url, string user, string password, string database_name, int port, int max_conns)
{
	m_url = url;
	m_port = port;
	m_port_s = to_string(port);   //需要c++11支持
	m_user = user;
	m_password = password;
	m_database_name = database_name;

	for (int i = 0; i < max_conns; ++i)
	{
		MYSQL* conn = NULL;
		conn = mysql_init(conn);

		if (NULL == conn)
		{
			LOG_ERROR("MySQL init error, idx:%d, func:%s, line:%d\n",i,__FUNCTION__,__LINE__);   //连接Mysql数据库失败
			return false;
		}

		conn = mysql_real_connect(conn,m_url.c_str(),m_user.c_str(),m_password.c_str(),m_database_name.c_str(),m_port,NULL,0);
		if (NULL == conn)
		{
			LOG_ERROR("MySQL connection error, idx:%d, func:%s, line:%d\n",i,__FUNCTION__,__LINE__);
			return false;
		}

		mysql_conn_list.push_back(conn);
		++m_free_conns;
	}

	sem = Sem(m_free_conns);         //初始化信号量，标识当前连接池的可用连接数量]
	m_max_conns = m_free_conns;      //连接池中总的连接数量

	return true;
}

/*
* 
* 销毁数据库连接池
* 
*/
bool SqlConnectionPool::destory_pool()
{
	if (mysql_conn_list.size() == 0)
	{
		return true;
	}

	m_mutex.lock();

	if (mysql_conn_list.size() == 0)
	{
		m_mutex.unlock();
		return true;
	}

	list<MYSQL*>::iterator it;
	for (it = mysql_conn_list.begin(); it != mysql_conn_list.end(); ++it)
	{
		MYSQL* conn = *it;
		mysql_close(conn);
	}
	m_cur_conns = 0;
	m_free_conns = 0;

	mysql_conn_list.clear();   //清空

	m_mutex.unlock();

	return true;
}

/*
* 
* 从数据库连接池中返回一个连接
* 
*/
MYSQL* SqlConnectionPool::get_connection()
{
	MYSQL* conn = NULL;

	if (0 == mysql_conn_list.size())
	{
		return NULL;
	}

	sem.wait();          //信号量判断有没有
	m_mutex.lock();      //有就拿锁

	conn = mysql_conn_list.front();
	mysql_conn_list.pop_front();
	--m_free_conns;      //可用数量减一
	++m_cur_conns;       //已使用数量加一

	m_mutex.unlock();

	return conn;
}

/*
* 
* 将一个MYSQL连接放回数据库连接池
* 
*/
bool SqlConnectionPool::release_connection(MYSQL* conn)
{
	if (conn == NULL)
		return false;
	
	m_mutex.lock();

	if (conn == NULL)
	{
		m_mutex.unlock();
		return false;
	}
	else
	{
		mysql_conn_list.push_back(conn);
		++m_free_conns;
		--m_cur_conns;
	}

	m_mutex.unlock();
	sem.post();
	return true;
}


/*
* 
* RAII方式
* 
*/
ConnectionRAII::ConnectionRAII(MYSQL** conn, SqlConnectionPool* pool)
{
	m_pool = pool;

	*conn = pool->get_connection();
	m_conn = *conn;
}

/*
* 
* RAII方式
* 
*/
ConnectionRAII::~ConnectionRAII()
{
	m_pool->release_connection(m_conn);
}

