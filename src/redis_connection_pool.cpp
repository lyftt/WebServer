#include "redis_connection_pool.h"
#include "log.h"

//��̬��������
Locker RedisConnectionPool::m_lock;
RedisConnectionPool* RedisConnectionPool::m_instance = NULL;

RedisConnectionPool::RedisConnectionPool()
{
	m_cur_conns = 0;
	m_free_conns = 0;
	m_max_conns = 0;
}

RedisConnectionPool::~RedisConnectionPool()
{
	destory_pool();
}

bool RedisConnectionPool::init(string url, int port, int max_conns)
{
	m_url = url;
	m_port = port;
	m_port_s = to_string(m_port);

	for (int i = 0; i < max_conns; ++i)
	{
		redisContext* conn = redisConnect(url.c_str(),m_port);
		if (conn == NULL || conn->err != 0)
		{
			LOG_ERROR("redisConnect() error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
			return false;
		}

		redis_pool_obj temp_obj(conn);
		m_redis_pool.push_back(temp_obj);

		m_free_conns++;
	}

	m_max_conns = m_free_conns;
	m_sem = Sem(m_max_conns);

	return true;
}

/*
* 
* ����redis���ӳ�
* 
*/
bool RedisConnectionPool::destory_pool()
{
	if (m_max_conns == 0)
		return true;

	m_lock.lock();

	if (m_max_conns == 0)
		return true;

	for (int i = 0; i < m_redis_pool.size(); ++i)
	{
		redisFree(m_redis_pool[i].conn);      //�ͷ���������
	}

	m_redis_pool.clear();

	m_lock.unlock();

	return true;
}

/*
* 
* ��ȡ��һʵ��
* 
*/
RedisConnectionPool* RedisConnectionPool::GetInstance()
{
	if (m_instance)
	{
		return m_instance;
	}
	else
	{
		m_lock.lock();

		if (!m_instance)
		{
			m_instance = new (std::nothrow) RedisConnectionPool;
		}

		m_lock.unlock();
	}

	return m_instance;
}

/*
* 
* ����ɾ��
* 
*/
void RedisConnectionPool::RevInstance()
{
	if (m_instance)
	{
		m_lock.lock();
		
		if (m_instance)
		{
			delete m_instance;
			m_instance = NULL;
		}

		m_lock.unlock();
	}
}

/*
* 
*�ӳ��л�ȡһ������ 
* 
*/
redisContext* RedisConnectionPool::get_connection()
{
	redisContext* conn = NULL;

	if (m_redis_pool.size() == 0)
	{
		LOG_ERROR("redis pool have no unused connection now, func:%s, line:%d\n",__FUNCTION__,__LINE__);
		return NULL;
	}

	m_sem.wait();
	m_lock.lock();

	if (m_redis_pool.size() == 0)
	{
		LOG_ERROR("redis pool have no unused connection now, func:%s, line:%d\n", __FUNCTION__, __LINE__);
		m_lock.unlock();
		return NULL;
	}

	int i = 0;

	for (; i < m_redis_pool.size(); ++i)
	{
		if (m_redis_pool[i].conn_use_status == UNUSED)
		{
			conn = m_redis_pool[i].conn;             //�õ�����
			m_redis_pool[i].conn_use_status = USED;  //ʹ��״̬����
			m_free_conns--;                          //ʣ��������һ
			m_cur_conns++;                           //��ʹ��������һ
			break;
		}
	}

	if (i == m_redis_pool.size())
	{
		LOG_ERROR("redis pool have no unused connection now, func:%s, line:%d\n",__FUNCTION__,__LINE__);
	}

	m_lock.unlock();

	return conn;
}

/*
* 
* �ͷ�һ�����ӣ��Ż����ӳ�
* 
*/
bool RedisConnectionPool::release_connection(redisContext* conn)
{
	if (conn == NULL)
	{
		return false;
	}

	m_lock.lock();
	int i = 0;

	for (; i < m_redis_pool.size(); ++i)
	{
		if (m_redis_pool[i].conn == conn)
		{
			m_redis_pool[i].conn_use_status = UNUSED;
			m_cur_conns--;
			m_free_conns++;
			break;
		}
	}

	if (i == m_redis_pool.size())
	{
		LOG_ERROR("this conn not exists in  redis pool, func:%s, line:%d\n", __FUNCTION__,__LINE__);
		m_lock.unlock();
		return false;
	}

	m_lock.unlock();
	m_sem.post();

	return true;
}


/*
* 
* 
* 
*/
RedisConnRAII::RedisConnRAII(redisContext** conn, RedisConnectionPool* pool)
{
	m_pool = pool;

	*conn = m_pool->get_connection();
	m_conn = *conn;
}

/*
* 
* 
* 
*/
RedisConnRAII::~RedisConnRAII()
{
	m_pool->release_connection(m_conn);
}


