#ifndef  __LOG_H__
#define  __LOG_H__

#include <iostream>
#include <stdio.h>
#include "block_queue.h"
#include "locker.h"
#include <pthread.h>

using namespace std;

/*是否关闭日志的开关*/
extern int g_close_log;

class Log
{
public:
	static Log* get_instance()
	{
		if (!m_instance)   //双检测
		{
			m_mutex.lock();   //未必能最先拿到锁,所以拿到锁之后需要再做一次判断
			if (!m_instance)
			{
				m_instance = new (std::nothrow) Log;   //可能返回NULL,即new失败的情况
			}
			m_mutex.unlock();
		}

		return m_instance;
	}

	//线程工作函数
	static void* flush_log_thread(void* args)
	{
		Log::get_instance()->async_write_log();   //异步写日志
	}

	//初始化
	bool init(const char* file_name,int log_buf_size = 8192,int split_lines=5000000,int max_queue_size = 0);
	
	//写日志，可能同步写入，也可能异步的写入阻塞队列中
	bool write_log(int level,const char* format,...);

	//刷新缓冲区的内容到日志文件
	bool flush();

private:
	static Log* m_instance;     
	static Locker m_mutex;

private:
	Log();
	virtual ~Log();

	//异步写日志
	bool async_write_log()
	{
		string single_log;

		while (true)
		{
			if(m_log_queue->pop(single_log))
			{
				m_lock.lock();
				fputs(single_log.c_str(), m_fp);   //写入日志文件
				m_lock.unlock();
			}
		}
	}


private:
	char dir_name[128];    //日志目录
	char log_name[128];    //日志文件
	int m_max_lines;       //日志文件中最大的行数
	int m_counts;          //当前日志数量
	int m_today;           //记录当天是那一天
	FILE* m_fp;            //文件指针
	char* m_buf;           //日志的缓冲区
	int m_logbuf_size;     //日志缓冲区大小

	BlockQueue<string>* m_log_queue;   //阻塞队列实现的日志队列
	bool m_is_async;       //是否开启异步日志
	Locker m_lock;         //互斥锁
};

//可变参数宏，__VA_ARGS__用来表示宏的可变参数部分，##主要用来处理 
// #define  my_print2(fmt,...)  printf(fmt, ##__VA_ARGS__)
// 如果上面的宏的...部分没有可变参数的话，##用来消除前面的逗号","
// 例如 my_print("1111111\n")
#define LOG_DEBUG(format,...)  if(0 == g_close_log) {Log::get_instance()->write_log(0,format,##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format,...)  if(0 == g_close_log) {Log::get_instance()->write_log(1,format,##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format,...)  if(0 == g_close_log) {Log::get_instance()->write_log(2,format,##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format,...)  if(0 == g_close_log) {Log::get_instance()->write_log(3,format,##__VA_ARGS__); Log::get_instance()->flush();}

#define LOG_START()  do{g_close_log = 0;} while(0)
#define LOG_STOP()  do{g_close_log = 1;} while(0)

#endif
