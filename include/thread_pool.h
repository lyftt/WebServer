#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <list>
#include <iostream>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include "locker.h"
#include "sql_connection_pool.h"
#include "log.h"
#include "config.h"

template<typename T>
class ThreadPool
{
public:
	ThreadPool(int actor_model, SqlConnectionPool* conn_pool, int thread_number = 8,int max_request = 10000);
	virtual ~ThreadPool();

	//向队列中添加一个请求待处理的请求对象
	bool append(T* request,int state);
	bool append_p(T* resuest);

private:
	static void* worker(void* arg);    //线程的工作函数,必须是静态的
	void run();                        //

private:
	int m_thread_number;          //线程池中线程数量
	int m_max_requests;           //请求队列中最多的请求数量
	pthread_t* m_threads;         //线程池的数组，大小为m_thread_number
	std::list<T*> m_request_queue;    //请求队列
	Locker m_queue_locker;            //保护请求队列的互斥锁
	Sem m_queue_sem;                  //请求队列的信号量
	SqlConnectionPool* m_conn_pool;   //mysql连接池
	int m_actor_model;                //模型切换
};

//默认参数只能在声明或定义2者选择一处即可，不能都有
template<typename T>
ThreadPool<T>::ThreadPool(int actor_model, SqlConnectionPool* conn_pool, int thread_number, int max_request):m_actor_model(actor_model),m_thread_number(thread_number),m_max_requests(max_request),m_threads(NULL),m_conn_pool(conn_pool)
{
	int thread_fail_num = 0;
	if (thread_number <= 0 || max_request <= 0)
	{
		throw std::exception();
		//后面需要加日志
	}

	m_threads = new (std::nothrow) pthread_t[thread_number];  //为每个线程id分配动态数组
	if (!m_threads)
	{
		throw std::exception();
	}

	for (int i = 0; i < m_thread_number; ++i)
	{
		if (pthread_create(m_threads + i, NULL, worker, this) != 0)
		{
			thread_fail_num++;      //创建线程失败
		}
		else
		{
			if (pthread_detach(m_threads[i]) != 0)     //分离线程失败
			{
				thread_fail_num++;
			}
		}
	}

	if (thread_fail_num > (m_thread_number / 2))   //失败线程过多
	{
		if(m_threads)
			delete[] m_threads;     //释放动态内存
		throw std::exception();
	}
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
	if (m_threads)
	{
		delete[] m_threads;   //释放动态内存
	}
}

template<typename T>
bool ThreadPool<T>::append(T* request, int state)
{
	//加锁
	m_queue_locker.lock();
	//请求队列是否已经满
	if (m_request_queue.size() >= m_max_requests)  
	{
		//解锁
		m_queue_locker.unlock();
		return false;
	}

	request->m_state = state;
	m_request_queue.push_back(request);   //放入请求队列
	//释放锁
	m_queue_locker.unlock();
	//信号量加1
	m_queue_sem.post();
	return true;
}

template<typename T>
bool ThreadPool<T>::append_p(T* request)
{
	//上锁
	m_queue_locker.lock();
	//判断请求队列是否已经满
	if (m_request_queue.size() >= m_max_requests)
	{
		//解锁
		m_queue_locker.unlock();
		return false;
	}

	m_request_queue.push_back(request);
	m_queue_locker.unlock();
	m_queue_sem.post();
	return true;
}

/*
*线程的工作函数
*/
template<typename T>
void* ThreadPool<T>::worker(void* arg)
{
	//ThreadPool<T>* pool = (ThreadPool<T>*)arg;
	ThreadPool<T>* pool = reinterpret_cast<ThreadPool<T>*>(arg);
	pool->run();

	return pool;
}

template<typename T>
void ThreadPool<T>::run()
{
	while (true)
	{
		//判断是否有数据
		m_queue_sem.wait();
		//从请求队列中取请求之前先上锁
		m_queue_locker.lock();
		
		//判断请求队列是否为空
		if (m_request_queue.empty())
		{
			m_queue_locker.unlock();
			continue;
		}

		//从请求队列取出头部的请求
		T* request = m_request_queue.front();
		m_request_queue.pop_front();
		//解锁
		m_queue_locker.unlock();

		if (!request)
		{
			continue;
		}

		//reactor模式，既需要处理读，也需要处理写
		if (m_actor_model == RE_ACTOR)
		{
			/*由于reactor自己处理读写，所以需要判断是读还是写*/
			if (request->m_state == 0)       //读事件
			{
				if (request->read_once())    //读数据成功
				{
					request->improv = 1;
					ConnectionRAII mysql_conn(&request->mysql,m_conn_pool);   //从mysql连接池中获取一个连接
					request->process();
				}
				else    //读数据失败
				{
					request->improv = 1;
					request->timer_flag = 1;   //失败标记
					LOG_ERROR("pthread id:%d, REACTOR, process read, read_once() error, func:%s, line:%d\n",syscall(224),__FUNCTION__,__LINE__);
				}
			}
			else if(request->m_state == 1)     //写事件
			{
				if (request->write())         
				{
					request->improv = 1;
				}
				else                           //写数据不成功
				{
					request->improv = 1;
					request->timer_flag = 1;   //失败标记
					LOG_ERROR("pthread id:%d, REACTOR, process write error, func:%s, line:%d\n", syscall(224), __FUNCTION__, __LINE__);
				}
			}
			else
			{
				LOG_ERROR("pthread id:%d, REACTOR, unknown m_state, func:%s, line:%d\n",syscall(224),__FUNCTION__,__LINE__);
			}
		}
		//procator模式，只需要处理逻辑，读写都有主线程来完成
		else
		{
			ConnectionRAII mysql_conn(&request->mysql,m_conn_pool);    //从mysql连接池中获取一个连接
			request->process();           //proactor只需要处理，不需要读写
		}
	}
}

#endif