#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <string>

#include "thread_pool.h"
#include "http_conn.h"
#include "TimerSortList.h"
#include "Timer.h"
#include "sql_connection_pool.h"
#include "redis_connection_pool.h"

const int MAX_FD = 65535;                //最大文件描述符数量
const int MAX_EVENT_NUMBER = 10000;      //最大事件数量
const int TIMESLOT = 5;                  //最小超时单位


class WebServer
{
public:
	WebServer();
	~WebServer();

	void init(int port,string user,string pass_word,string database_name,
			  int log_write,int opt_linger,int trig_mode,int sql_num,
			  int thread_num,int close_log,int actor_mode,int redis_num);

	/*线程池初始化*/
	bool thread_pool_init();

	/*数据库连接池初始化*/
	bool sql_pool_init();

	/*redis池初始化*/
	bool redis_pool_init();

	/*日志模块初始化*/
	bool log_init();

	/*事件触发模式*/
	bool trig_mode();

	/*开启事件监听*/
	bool event_listen();

	/*开启事件循环*/
	bool event_loop();

	/*定时器设定*/
	void timer(int connfd,struct sockaddr_in client_address);   //给新到的连接添加定时器
	bool adjust_timer(TimerUnit *timer);                   //调整TCP连接关联的定时器
	bool del_timer(TimerUnit *timer, int sockfd);          //删除TCP连接关联的定时器

	/*各种事件的处理函数*/
	bool deal_client_data();                                 //处理新到的连接
	bool deal_with_signal(bool& timeout,bool& stop_server);  //处理到达的信号
	bool deal_with_read(int sockfd);                         //处理读事件
	bool deal_with_write(int sockfd);                        //处理写事件

public:
	//基础部分
	int m_port;
	char* m_root;              //root文件夹路径
	int m_log_write;           //日志写入模式
	int m_close_log;           //日志是否关闭
	int m_actor_model;         //反应堆模式,reactor，proactor

	int m_pipefd[2];          //用来统一事件源的管道
	int m_epollfd;            //Webserver的epoll的标识
	http_conn* users;         //http连接对象数组

	//数据库相关
	SqlConnectionPool* m_conn_pool;    //数据库连接池
	string m_user;
	string m_password;
	string m_database_name;
	int m_sql_num;                     //数据库连接池最大连接数量

	//线程池相关
	ThreadPool<http_conn>* m_pool;          //线程池处理http_conn对象
	int m_thread_num;                       //线程池线程数量

	//redis连接池
	RedisConnectionPool* m_redis_pool;  
	int m_redis_num;

	//epoll相关
	epoll_event events[MAX_EVENT_NUMBER];   //

	int m_listenfd;           //监听套接字
	int m_opt_linger;         //优雅关闭套接字
	int m_trig_mode;
	int m_listen_trig_mode;   //监听套接字事件触发方式
	int m_conn_trig_mode;     //连接套接字事件触发方式

	//定时器相关
	client_data* users_timer;          //TCP连接相关的定时器的数组
	TimerSortList sort_timer_list;     //升序链表定时器
};

#endif
