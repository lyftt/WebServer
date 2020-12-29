#ifndef __CONFIG_H__
#define __CONFIG_H__

#define SQL_NUM        8        //数据库连接池连接数量
#define THREAD_NUM     8        //线程池线程数量

//日志
enum
{
	LOG_SYNC,     //日志同步
	LOG_ASYNS,    //日志异步
};

enum
{
	LOG_ON,       //日志打开,默认打开
	LOG_OFF,      //日志关闭
};

//触发模式组合
enum
{
	LT_LT,      //LT+LT
	LT_ET,      //LT+ET
	ET_LT,      //ET+LT
	ET_ET,      //ET+ET
};

//触发模式
enum
{
	LT,
	ET,
};

//是否优雅关闭
enum
{
	NORM_CLOSE,    //不优雅关闭连接
	NICE_CLOSE,    //优雅关闭
};

//反应堆类型
enum
{
	PRO_ACTOR,   //Proactor模型
	RE_ACTOR,    //Reactor模型
};

class Config
{
public:
	Config();
	~Config() {};

	//解析server启动的命令行参数
	void parse_args(int argc, char *argv[]);

	//Server的端口号
	int port;

	//日志写入模式（同步/异步）
	int log_write_mode;

	//组合触发模式
	int trig_mode;

	//listen套接字触发模式
	int listen_trig_mode;

	//conn套接字触发模式
	int conn_trig_mode;

	//优雅关闭连接
	int opt_linger;

	//数据库连接池内连接数量
	int sql_conn_num;

	//线程池连接数量
	int thread_num;

	//是否关闭日志
	int close_log;

	//并发模型选择
	int actor_mode;
};

#endif