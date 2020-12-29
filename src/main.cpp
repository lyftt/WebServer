#include <iostream>
#include "config.h"
#include "thread_pool.h"
#include "log.h"
#include "webserver.h"


int main(int argc,char * argv[])
{
	/*数据库信息,登录名,密码,库名*/
	string user = "root";
	string passwd = "./Lyf178nba";
	string database_name = "yourdb";

	/*解析命令行*/
	/*
	* 默认端口9006
	* 日志默认打开
	* 日志默认同步模式
	* 组合触发LT+LT
	* listen套接字默认LT触发
	* conn套接字默认LT触发
	* 默认不使用优雅断开连接
	* 数据库连接池数量默认8
	* 线程池线程数量默认8
	* 反应堆模型默认PROACTOR
	* 
	*/
	Config config;
	config.parse_args(argc,argv);   //解析命令行参数


	/*Webserver 初始化*/
	WebServer server;
	server.init(config.port,user,passwd,database_name,config.log_write_mode,
				config.opt_linger,config.trig_mode,config.sql_conn_num,config.thread_num,
				config.close_log,config.actor_mode,8);


	if (!server.log_init())          //日志初始化失败
	{
		LOG_ERROR("log_init error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
		exit(-1);
	}

	if (!server.redis_pool_init())
	{
		LOG_ERROR("redis_pool_init error, func:%s, line:%d\n", __FUNCTION__, __LINE__);
		exit(-1);
	}

	if (!server.sql_pool_init())
	{
		LOG_ERROR("sql_pool_init error, func:%s, line:%d\n", __FUNCTION__, __LINE__);
		exit(-1);
	}

	if (!server.thread_pool_init())  //线程池初始化失败
	{
		LOG_ERROR("thread_pool_init error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
		exit(-1);
	}

	server.trig_mode();        //触发模式初始化

	server.event_listen();     //事件循环初始化

	server.event_loop();       //开启事件循环

	return 0;
}
