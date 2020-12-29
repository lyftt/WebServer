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

	/*��ʼ��*/
	bool init(string url,int port,int max_conns);

	/*�����������ӳ�*/
	bool destory_pool();

	/*�ӳ����л�ȡһ������*/
	redisContext* get_connection();

	/*�ͷ�һ�����ӣ����·Ż����ӳ�*/
	bool release_connection(redisContext* conn);

	/*����*/
	static RedisConnectionPool* m_instance;    //����redis���ӳض���
	static RedisConnectionPool* GetInstance(); //��ȡ���ӳ�
	static void RevInstance();

private: 
	string m_url;          //redis��������ַ
	string m_port_s;       //redis�������˿ں�
	int m_port;            //�˿ںţ�����

	int m_max_conns;       //������������
	int m_free_conns;      //ʣ����������
	int m_cur_conns;       //�Ѿ�ʹ����������

	vector<redis_pool_obj> m_redis_pool;  //��

	Sem m_sem;              //�ź���
	static Locker m_lock;   //��
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