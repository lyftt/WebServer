#ifndef __BLOCK_H__
#define __BLOCK_H__

#include "locker.h"
#include <exception>
#include <time.h>
#include <sys/time.h>
#include "pthread.h"

template<typename T>
class BlockQueue
{
public:
	BlockQueue(int max_size = 1000)
	{
		if (max_size <= 0)
		{
			throw std::exception();
		}

		m_max_size = max_size;   //队列的最大容量
		m_size = 0;
		m_front = -1;
		m_back = -1;
		m_array = new (std::nothrow) T[m_max_size];

		if (NULL == m_array)
		{
			throw std::exception();
		}
	}

	~BlockQueue()
	{
		m_lock.lock();

		if (m_array)
		{
			delete[] m_array;
		}

		m_lock.unlock();
	}

	void clear()
	{
		m_lock.lock();

		m_size = 0;
		m_front = -1;
		m_back = -1;

		m_lock.unlock();
	}

	bool full()
	{
		m_lock.lock();

		if (m_size >= m_max_size)
		{
			m_lock.unlock();
			return true;
		}
			
		m_lock.unlock();
		return false;
	}

	bool empty()
	{
		m_lock.lock();

		if (m_size == 0)
		{
			m_lock.unlock();
			return true;
		}

		m_lock.unlock();
		return false;
	}
	
	//返回队首元素的引用
	bool front(T& value)
	{
		m_lock.lock();

		if (0 == m_size)
		{
			m_lock.unlock();
			return false;
		}

		value = m_array[m_front];
		m_lock.unlock();
		return true;
	}

	//返回队列尾元素的引用
	bool back(T& value)
	{
		m_lock.lock();

		if (0 == m_size)
		{
			m_lock.unlock();
			return false;
		}

		value = m_array[m_back];
		m_lock.unlock();
		return true;
	}

	//返回队列当前大小
	int size()
	{
		int temp = 0;
		m_lock.lock();

		temp = m_size;

		m_lock.unlock();
		return temp;
	}

	int max_size()
	{
		int temp = 0;
		m_lock.lock();

		temp = m_max_size;

		m_lock.unlock();
		return temp;
	}

	bool push(const T& item)
	{
		m_lock.lock();

		if (m_size >= m_max_size)
		{
			m_cond.broadcast();      //此时队列已经满了，应该唤醒所有等待的线程去取
			m_lock.unlock();
			return false;
		}

		m_back = (m_back + 1) % m_max_size;
		m_array[m_back] = item;
		m_size++;

		m_cond.broadcast();    //放入之后唤醒所有线程来取
		m_lock.unlock();
		return true;
	}

	//弹出队列首部元素，如果队列没有元素可取，则会等待信号量
	bool pop(T& item)
	{
		m_lock.lock();
		 
		while (m_size <= 0)   //可能同时被唤醒，但是较晚拿到互斥锁，这时数据可能已经被取走了
		{
			if (!m_cond.wait(m_lock.get_mutex()))   //等待条件变量的信号失败
			{
				m_lock.unlock();
				return false;
			}
		}

		m_front = (m_front + 1) % m_max_size;
		item = m_array[m_front];
		m_size--;

		m_lock.unlock();
		return true;
	}

	//超时版本，最多等待ms_timeout毫秒
	bool pop(T& item,int ms_timeout)
	{
		struct timeval now = {0,0};
		struct timespec t = {0,0};
		gettimeofday(&now,NULL);
		m_lock.lock();

		if (m_size <= 0)
		{
			t.tv_sec = now.tv_sec = ms_timeout / 1000;
			t.tv_nsec = (ms_timeout % 1000) * 1000;
			if (!m_cond.time_wait(m_lock.get_mutex(),t))   //超时则会自动被唤醒，而不是无限等待
			{
				m_lock.unlock();
				return false;
			}
		}

		if (m_size <= 0)   
		{
			m_lock.unlock();
			return false;
		}

		m_front = (m_front + 1) % m_max_size;
		item = m_array[m_front];
		m_size--;

		m_lock.unlock();
		return true;
	}

private:
	Locker m_lock;          //队列的互斥锁
	Cond m_cond;            //信号量

	T* m_array;         //数组，队列的底层数据结构
	int m_size;         //队列目前大小
	int m_max_size;     //队列的最大容量
	int m_front;        //队列头指针
	int m_back;         //队列尾指针
};

#endif