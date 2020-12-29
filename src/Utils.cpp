#include "Utils.h"
#include <fcntl.h>
#include <sys/epoll.h>
#include "config.h"
#include "http_conn.h"
#include "TimerSortList.h"
#include "webserver.h"

/*静态遍历定义*/
int Utils::u_epollfd = -1;

/*
*
* 设置fd为非阻塞
*
*/
int Utils::setnonblocking(int fd)
{
	int old_opt = fcntl(fd,F_GETFL);
	int new_opt = old_opt | O_NONBLOCK;
	fcntl(fd,F_SETFL,new_opt);
	return old_opt;
}

/*
*
*重置文件描述的epolloneshot事件
* 
*/
bool Utils::modfd(int epollfd, int fd, int ev, int trig_mode)
{
	epoll_event event;
	event.data.fd = fd;

	if (ET == trig_mode)
	{
		event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;   //ET 触发模式
	}
	else
	{
		event.events = ev | EPOLLONESHOT | EPOLLRDHUP;             //LT 触发模式
	}

	epoll_ctl(u_epollfd,EPOLL_CTL_MOD,fd,&event);     //重置epolloneshot事件

}

/*
*将文件描述符注册到epoll内核事件表
* 
*/
bool Utils::addfd(int epollfd, int fd, bool one_shot, int trig_mode)
{
	epoll_event event;
	event.data.fd = fd;

	if (ET == trig_mode)
	{
		event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;      //高效ET模式,监听可读事件和连接断开事件
	}
	else 
	{
		event.events = EPOLLIN | EPOLLRDHUP;        //普通LT模式,监听可读事件和连接断开事件
	}


	if (one_shot)
		event.events |= EPOLLONESHOT;

	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);

	setnonblocking(fd);

	return true;
}

/*
* 
* 设置epollfd
* 
**/
int Utils::set_epollfd(int epollfd)
{
	int old_value = u_epollfd;
	u_epollfd = epollfd;

	return old_value;
}

/*
* 
* 获取epollfd
* 
**/
int Utils::get_epollfd()
{
	return u_epollfd;
}


/*
*
* 定时器的回调函数
* 
*/
void Utils::cb_func(client_data* user_data)
{
	if (!user_data)
		return;

	epoll_ctl(u_epollfd,EPOLL_CTL_DEL,user_data->sockfd,0);    //从监听的红黑树中删除这个连接
	close(user_data->sockfd);                                  //关闭连接
	http_conn::m_user_count--;                                 //连接数量减一
}

/*
* 
* 还没绑定到定时器时，直接调用close关闭连接即可
* 
*/
void Utils::show_error(int connfd,const char *info)
{
	send(connfd,info,strlen(info),0);
	close(connfd);
}

/*
* 
* 超时的处理函数
* 
*/
void Utils::timer_handler(TimerSortList* list)
{
	list->tick();
	alarm(TIMESLOT);
}

