#ifndef __TIMER_H__
#define __TIMER_H__

#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>
#include <fcntl.h>

//定时器类型
#define LIST_TIMER         0x01               //升序链表定时器
#define POLL_TIMER         0x02               //时间轮定时器
#define HEAP_TIMER         0x03               //最小堆定时器

/*
* 客户端数据结构
* 
*/
class TimerUnit;
struct client_data
{
	sockaddr_in address;         //客户端的地址结构
	int sockfd;                  //客户端连接描述符
	TimerUnit* timer;            //所属的定时器单元
};

class TimerUnit
{
public:
	TimerUnit() :prev(NULL), next(NULL), user_data(NULL),cb_func(NULL) {}
	~TimerUnit() {}

public:
	time_t expire;                    //超时时间

	void (*cb_func)(client_data *);   //时间到期后的回调函数指针
	client_data* user_data;           //客户端数据结构
	TimerUnit* prev;  
	TimerUnit* next;
};

/*
* 
* 抽象基类
* 
**/
class TimerBase
{
public:
	TimerBase() {}
	~TimerBase() {}

	/*添加定时器，纯虚函数*/
	virtual bool add_timer(TimerUnit* timer) = 0;

	/*调整定时器，纯虚函数*/
	virtual bool adjust_timer(TimerUnit* timer) = 0;

	/*删除定时器，纯虚函数*/
	virtual bool del_timer(TimerUnit* timer) = 0;

	/*滴答函数，纯虚函数*/
	virtual void tick() = 0;

};


#endif