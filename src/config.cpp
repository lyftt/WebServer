//#include "config.h"
#include "config.h"
#include <getopt.h>
#include <cstdlib>
#include <iostream>

Config::Config()
{
	//各个参数的默认值
	//默认端口号
	port = 9006;

	//日志异步写入方式，默认同步
	log_write_mode = LOG_SYNC;

	//触发组合模式，默认 LT+LT
	trig_mode = LT_LT;

	//listen套接字触发模式，默认LT
	listen_trig_mode = LT;

	//conn套接字触发模式，默认LT
	conn_trig_mode = LT;

	//是否优雅关闭连接,默认不使用
	opt_linger = NORM_CLOSE;

	//数据库连接池连接数量
	sql_conn_num = SQL_NUM;

	//线程池线程数量
	thread_num = THREAD_NUM;

	//关闭日志，默认不关闭
	close_log = LOG_ON;

	//并发模型，默认proactor
	actor_mode = PRO_ACTOR;
}

//解析
void Config::parse_args(int argc,char * argv[])
{
	int opt;
	const char *opt_str = "p:l:m:o:s:t:c:a:";   //合法的选项字符
	
	while((opt = getopt(argc,argv,opt_str)) != -1)
	{
		switch (opt)
		{
			case 'p':  //端口号参数
			{
				port = atoi(optarg);       //opt是选项的参数
				std::cout << "SET port:" << port << std::endl;
				break;
			}
			case 'l':  //日志模式参数
			{
				log_write_mode = atoi(optarg);
				std::cout << "SET log_write_mode:" << log_write_mode << " " << (log_write_mode == LOG_SYNC ? "SYNC" : "ASYNC") << std::endl;
				break;
			}
			case 'm':  //组合触发模式
			{
				trig_mode = atoi(optarg);
				std::cout << "SET trig_mode:" << trig_mode << " " << std::endl;
				break;
			}
			case 'o':  //是否优雅关闭连接
			{
				opt_linger = atoi(optarg);
				std::cout << "SET opt_linger:" << opt_linger << std::endl;
				break;
			}
			case 's':  //数据库连接池中连接的数量
			{
				sql_conn_num = atoi(optarg);
				std::cout << "SET sql_conn_num:" << sql_conn_num << std::endl;
				break;
			}
			case 't':  //线程池中线程的数量
			{
				thread_num = atoi(optarg);
				std::cout << "SET thread_num:" << thread_num << std::endl;
				break;
			}
			case 'c':  //是否关闭日志
			{
				close_log = atoi(optarg);
				std::cout << "SET close_log:" << close_log << std::endl;
				break;
			}
			case 'a':  //并发模式
			{
				actor_mode = atoi(optarg);
				std::cout << "SET actor_mode:" << actor_mode << std::endl;
				break;
			}
			default:
			{
				std::cout << "GET a unknown option" << std::endl;
				break;
			}
		}
	}
}


