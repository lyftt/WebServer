#ifndef __HTTP_CONN_H__
#define __HTTP_CONN_H__

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>
#include <string>

#include "sql_connection_pool.h"
#include "redis_connection_pool.h"

using namespace std;


class http_conn
{
public:
	/*一些静态常量定义*/
	static const int FILENAME_LEN = 200;                //要读取的文件的名称的大小
	static const int READ_BUFFER_SIZE = 2048;           //读缓冲区的大小
	static const int WRITE_BUFFER_SIZE = 2048;          //写缓冲区的大小
	static map<string, string> m_users;                 //登录用户的缓存

	/*http的方法，只使用GET和POST*/
	enum METHOD
	{
		GET = 0,
		POST,
		HEAD,
		PUT,
		DELETE,
		TRACE,
		OPTIONS,
		CONNECT,
		PATH
	};

	/*http主状态机状态*/
	enum CHECK_STATE
	{
		CHECK_STATE_REQUESTLINE = 0,   //解析到请求行
		CHECK_STATE_HEADER,            //解析到请求头部
		CHECK_STATE_CONTENT            //解析到请求体，仅用于POST亲求
	};

	/*http从状态机状态*/
	enum LINE_STATUS
	{
		LINE_OK,         //解析到一行
		LINE_BAD,        //解析行有错误
		LINE_OPEN        //还未完整的解析到一行
	};

	/*http状态码*/
	enum HTTP_CODE
	{
		NO_REQUEST,          //请求不完整，还需要继续获取数据
		GET_REQUEST,         //获得了完整的http请求
		BAD_REQUEST,         //HTTP请求语法有错误
		NO_RESUORCE,         //请求资源不存在
		FORBIDDEN_REQUEST,   //请求资源禁止访问，没有读取权限
		FILE_REQUEST,        //请求资源可以正常访问
		INTERNAL_ERROR,      //服务器内部错误
		CLOSED_CONNECTION    //
	};

public:
	http_conn() {}      //构造函数不做任何事情
	~http_conn() {}     //

public:
	/*初始化http连接对象*/
	bool init(int sockfd, const sockaddr_in& addr, char *root, int trig_mode, int close_log, 
			  string user, string password, string sqlname);

	/*关闭http连接*/
	bool close_conn(bool real_close = true);

	/**/
	void process();

	/*读取浏览器发送来的全部数据*/
	bool read_once();

	/*相应报文写入函数*/
	bool write();

	/**/
	sockaddr_in* get_address()
	{
		return &m_address;
	}

	/*加载数据库中信息到内存中*/
	bool init_mysql_result(SqlConnectionPool* conn_pool, RedisConnectionPool* redis_pool);

	/**/
	int timer_flag;
	int improv;


private:
	/*初始化http连接，工具函数*/
	void init();

	/*从m_read_buf读取数据，并处理http请求报文*/
	HTTP_CODE process_read();

	/*向m_write_buf写入http响应报文*/
	bool process_write(HTTP_CODE ret);

	/*主状态机解析报文中的请求行中的数据*/
	HTTP_CODE parse_request_line(char* text);

	/*主状态机解析报文中的请求头部*/
	HTTP_CODE parse_headers(char* text);

	/*主状态机解析报文中的请求体*/
	HTTP_CODE parse_content(char* text);

	/*生成响应报文*/
	HTTP_CODE do_request();

	/*后移指针，指向未处理的字符*/
	char* get_line() { return m_read_buf + m_start_line; };

	/*从状态机读取一行*/
	LINE_STATUS parse_line();

	/*每个http_conn会将要处理的文件映射到内存中来加快速度*/
	void unmap();

	/*根据响应报文格式，生成对应8个部分，以下函数均由do_request调用*/
	bool add_response(const char* format, ...);
	bool add_content(const char* content);
	bool add_status_line(int status, const char* title);
	bool add_headers(int content_length);
	bool add_content_type();
	bool add_content_length(int content_length);
	bool add_linger();
	bool add_blank_line();

public:
	static int m_epollfd;    //epoll标识符号
	static int m_user_count; //所有http连接的数量
	MYSQL* mysql;            //Mysql句柄
	int m_state;             //读为0, 写为1

private:
	int m_sockfd;                        //该http连接的socket描述符
	sockaddr_in m_address;               //该http连接的socket地址结构
	char m_read_buf[READ_BUFFER_SIZE];   //该http连接的读缓冲区
	int m_read_idx;                      //缓冲区中有效数据的最后一个字节的下一个位置
	int m_checked_idx;                   //当前的读取位置
	int m_start_line;                    //读取的行的起始位置
	char m_write_buf[WRITE_BUFFER_SIZE]; //该http连接的写缓冲区
	int m_write_idx;

	CHECK_STATE m_check_state;           //主状态机状态
	METHOD m_method;                     //http方法

	char m_real_file[FILENAME_LEN];      //html文件绝对路径名字
	char* m_url;                         //http的url
	char* m_version;                     //http的版本
	char* m_host;                        //http的host
	int m_content_length;                //请求体内容长度
	bool m_linger;                       //是否长连接
	char* m_file_address;                //将html文件映射到内存
	struct stat m_file_stat;             //响应的html文件的属性
	struct iovec m_iv[2];                //集中写的离散数组
	int m_iv_count;
	int cgi;                         //是否启用cgi
	char* m_string;                  //http请求中的请求体
	int bytes_to_send;               //要发送的字节数
	int bytes_have_send;             //已经发送的字节数
	char* doc_root;                  //html文件所在的根目录

	int m_trig_mode;                 //当前连接的触发模式
	int m_close_log;                 //日志功能，是否需要？

	/*SQL账号相关*/
	char sql_user[100];             
	char sql_passwd[100];
	char sql_name[100];

};

#endif
