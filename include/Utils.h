#ifndef __UTILS_H__
#define __UTILS_H__

#include "Timer.h"
#include "TimerSortList.h"

/*
*工具类，包含功能:
* (1)设置文件描述符非阻塞
* (2)添加文件描述符的可读事件监听到epoll
* 
*/
class Utils
{
public:
	Utils() {}
	~Utils() {}

	//设置fd非阻塞
	static int setnonblocking(int fd);

	//添加文件描述符的可读事件监听到epoll
	static bool addfd(int epollfd, int fd, bool one_shot, int trig_mode);

	//重置文件描述的epolloneshot事件
	static bool modfd(int epollfd, int fd, int ev, int trig_mode);

	//定时器到期后的回调函数
	static void cb_func(client_data* user_data);

	//设置epollfd
	static int set_epollfd(int epollfd);

	//获取epollfd
	static int get_epollfd();

	//会送结果，并断开连接
	static void show_error(int connfd, const char* info);

	//超时处理函数
	static void timer_handler(TimerSortList* list);

private:
	static int u_epollfd;    //epoll的标识
};


#endif
