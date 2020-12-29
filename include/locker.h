#ifndef __LOCKER_H__
#define __LOCKER_H__

#include <pthread.h>
#include <semaphore.h>
#include <exception>

//信号量封装
class Sem
{
public:
	Sem()
	{
		if (sem_init(&m_sem, 0, 0) != 0)
		{
			throw std::exception();
			//后面需要加上日志功能
		}
	}

	//指定信号量的初始值
	Sem(int num)
	{
		if (sem_init(&m_sem,0,num) != 0)  //设置信号量初始值
		{
			throw std::exception();
		}
	}

	~Sem()
	{
		if (sem_destroy(&m_sem) != 0)
		{
			throw std::exception();
		}
	}

	//信号量减1
	bool wait()
	{
		return sem_wait(&m_sem) == 0;
	}

	//信号量加1
	bool post()
	{
		return sem_post(&m_sem) == 0;
	}

private:
	sem_t m_sem;
};

//互斥量封装
class Locker
{
public:
	Locker()
	{
		if(pthread_mutex_init(&m_mutex,NULL) != 0)   //锁初始化
		{
			throw std::exception();
		}
	}

	~Locker()
	{
		if (pthread_mutex_destroy(&m_mutex) != 0)    //锁销毁
		{
			throw std::exception();
		}
	}

	bool lock()
	{
		return pthread_mutex_lock(&m_mutex) == 0;   //上锁
	}

	bool unlock()
	{
		return pthread_mutex_unlock(&m_mutex) == 0; //解锁
	}

	pthread_mutex_t* get_mutex()
	{
		return &m_mutex;
	}

private:
	pthread_mutex_t m_mutex;   //互斥锁
};

//条件变量封装
class Cond
{ 
public:
	Cond()
	{
		if (pthread_cond_init(&m_cond,NULL) != 0)
		{
			throw std::exception();
		}
	}

	~Cond()
	{
		if (pthread_cond_destroy(&m_cond) != 0)
		{
			throw std::exception();
		}
	}

	//等待信号量，需要pthread_mutex_t互斥锁进行配合
	//mutex用于保护条件变量
	bool wait(pthread_mutex_t * mutex)
	{
		//调用pthread_cond_wait之前，必须mutex已经被上锁
		//pthread_cond_wait执行的时候，会将当前线程加入到条件变量的等待队列中，然后解锁
		//从pthread_cond_wait开始执行，到其调用线程被放入条件变量的等待队列中这段时间内，
		//pthread_cond_signal和pthread_cond_broadcast将无法修改条件变量
		//pthread_cond_wait返回的时候，mutex将再次被锁上
		return pthread_cond_wait(&m_cond,mutex) == 0;
	}

	bool time_wait(pthread_mutex_t* mutex,struct timespec t)
	{
		return pthread_cond_timedwait(&m_cond,mutex,&t) == 0;
	}

	bool signal()
	{
		return pthread_cond_signal(&m_cond) == 0;
	}

	bool broadcast()
	{
		return pthread_cond_broadcast(&m_cond) == 0;
	}

private:
	pthread_cond_t m_cond;
};

#endif