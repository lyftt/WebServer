#include "log.h"
#include <cstring>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

//全局变量
int g_close_log = 0;

//类静态变量的定义和初始化
Log *Log::m_instance = NULL;
Locker Log::m_mutex;

//构造函数，简单初始化部分成员变量
Log::Log():m_counts(0), m_is_async(false), m_fp(NULL), m_buf(NULL),m_log_queue(NULL),m_logbuf_size(0)
{

}

Log::~Log()
{
	//释放阻塞队列
	if (m_log_queue)
	{
		delete m_log_queue;
		m_log_queue = NULL;
	}

	//关闭文件流
	if (m_fp)
	{
		fclose(m_fp);
		m_fp = NULL;
	}

	//释放日志缓冲
	if (m_buf)
	{
		delete []  m_buf;
		m_buf = NULL;
	}
}

//初始化
bool Log::init(const char* file_name, int log_buf_size, int split_lines, int max_queue_size)
{
	//配置了max_queue_size参数，标识开启异步日志
	if (max_queue_size >= 1)
	{
		m_is_async = true;     //开启异步日志
		m_log_queue = new (std::nothrow) BlockQueue<string>(max_queue_size);    //创建阻塞队列，并设置阻塞队列的大小

		/*设置异步的线程*/
		pthread_t pid;
		pthread_create(&pid,NULL,flush_log_thread,NULL);    //flush_log_thread为线程工作函数
	}

	m_logbuf_size = log_buf_size;         //日志缓冲大小
	m_max_lines = split_lines;            //最大行数
	m_buf = new char[m_logbuf_size];      //分配日志缓冲
	memset(m_buf,0,m_logbuf_size);        //清空
	 
	time_t t = time(NULL);                //获取当前时间(秒数)
	struct tm* sys_tm = localtime(&t);    //转换为日历时间
	struct tm my_tm = *sys_tm;

	const char* p = strrchr(file_name,'/');  //查找'/'最后一次出现的位置
	char log_full_name[256] = {0};           //日志文件全名

	if (p == NULL)
	{
		snprintf(log_full_name,255,"%d_%02d_%02d_%s",my_tm.tm_year + 1900,my_tm.tm_mon + 1,my_tm.tm_mday,file_name);
	}
	else
	{
		strcpy(log_name,p + 1);                          //设置日志文件名
		strncpy(dir_name,file_name,p - file_name + 1);   //设置日志文件的目录
		snprintf(log_full_name,255, "%s%d_%02d_%02d_%s",dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
	}

	m_today = my_tm.tm_mday;

	m_fp = fopen(log_full_name, "a");    //附加模式打开日志文件
	if (!m_fp)
	{
		return false;
	}

	return true;
}

//同步写日志
bool Log::write_log(int level, const char* format, ...)
{
	struct timeval now = {0,0};
	gettimeofday(&now,NULL);
	time_t t = now.tv_sec;               //获取现在的秒数
	struct tm* sys_tm = localtime(&t);
	struct tm my_tm = *sys_tm;           //现在的时间
	char s[16] = {0};                    //日志的开头部分

	switch (level)
	{
		case 0:
		{
			strcpy(s, "[Debug]:");
			break;
		}
		case 1:
		{
			strcpy(s, "[Info]:");
			break;
		}
		case 2:
		{
			strcpy(s, "[Warn]:");
			break;
		}
		case 3:
		{
			strcpy(s, "[Error]:");
			break;
		}
		default:
		{
			strcpy(s, "[Info]:");
			break;
		}
	}

	m_lock.lock();
	m_counts++;

	//时间不匹配或者文件的最大行数超了
	if (m_today != my_tm.tm_mday || m_counts % m_max_lines == 0)
	{
		char new_log[256] = {0};
		fflush(m_fp);
		fclose(m_fp);
		char time_mid[24] = { 0 };

		snprintf(time_mid,24,"%d_%02d_%02d_",my_tm.tm_year + 1900,my_tm.tm_mon + 1,my_tm.tm_mday);

		if (m_today != my_tm.tm_mday)
		{
			snprintf(new_log,255,"%s%s%s",dir_name,time_mid,log_name);
			m_today = my_tm.tm_mday;  //更新当前日
			m_counts = 0;             //新文件重新计数
		}
		else
		{
			snprintf(new_log,255,"%s%s%s.%lld",dir_name,time_mid,log_name,m_counts/m_max_lines);
		}

		m_fp = fopen(new_log,"a");
	}

	m_lock.unlock();

	//处理可变参数列表
	string log_str;  //要写入的日志
	va_list valst;
	va_start(valst,format);
		
	m_lock.lock();
	//日志的具体格式
	int n = snprintf(m_buf,48,"%d-%02d-%02d %02d:%02d:%02d.%06ld %s",my_tm.tm_year+1900,my_tm.tm_mon+1,
					my_tm.tm_mday,my_tm.tm_hour,my_tm.tm_min,my_tm.tm_sec,now.tv_usec,s);

	int m = vsnprintf(m_buf + n,m_logbuf_size - n - 1,format,valst);    //可变参数写入
	m_buf[n + m] = '\0';
	m_buf[n + m + 1] = '\0';
	log_str = m_buf;
		
	m_lock.unlock();

	if (m_is_async && !m_log_queue->full())    //异步且队列未满
	{
		m_log_queue->push(log_str);            //放入阻塞队列，阻塞队列是线程安全的
	}
	else
	{
		m_lock.lock();
		fputs(log_str.c_str(),m_fp);           //直接写入文件
		m_lock.unlock();
	}

	va_end(valst);

	return true;
}

bool Log::flush()
{
	m_lock.lock();
	fflush(m_fp);       //强制刷新
	m_lock.unlock();

	return true;
}