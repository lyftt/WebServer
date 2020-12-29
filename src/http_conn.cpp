#include "http_conn.h"
#include "Utils.h"
#include "config.h"
#include "log.h"
#include "mysql/mysql.h"
#include <sys/stat.h>
#include "hiredis.h"
#include "redis_connection_pool.h"

/*静态变量定义*/
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;          //目前http_conn连接对象的数量
map<string, string> http_conn::m_users;   //缓存所有用户的用户名和密码

/*锁*/
Locker locker;

/*HTTP有5种类型的状态码，具体的：

1xx：指示信息--表示请求已接收，继续处理。

2xx：成功--表示请求正常处理完毕。

200 OK：客户端请求被正常处理。

206 Partial content：客户端进行了范围请求。

3xx：重定向--要完成请求必须进行更进一步的操作。

301 Moved Permanently：永久重定向，该资源已被永久移动到新位置，将来任何对该资源的访问都要使用本响应返回的若干个URI之一。

302 Found：临时重定向，请求的资源现在临时从不同的URI中获得。

4xx：客户端错误--请求有语法错误，服务器无法处理请求。

400 Bad Request：请求报文存在语法错误。

403 Forbidden：请求被服务器拒绝。

404 Not Found：请求不存在，服务器上找不到请求的资源。

5xx：服务器端错误--服务器处理请求出错。

500 Internal Server Error：服务器在执行请求时出现错误。
*/

//200
const char* ok_200_title = "OK";
//400
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
//403
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file form this server.\n";
//404
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
//500
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

/*
* 
* 将Mysql数据从数据库加载到内存
* 
*/
bool http_conn::init_mysql_result(SqlConnectionPool* conn_pool, RedisConnectionPool* redis_pool)
{
	MYSQL* mysql = NULL;
	redisContext *redis = NULL;
	redisReply* reply = NULL;

	ConnectionRAII conn(&mysql,conn_pool);
	RedisConnRAII redis_conn(&redis,redis_pool);

	if (mysql_query(mysql,"SELECT username,passwd FROM user"))
	{
		LOG_ERROR("select error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
		return false;
	}

	MYSQL_RES* result = mysql_store_result(mysql);        //查询结果存储到本地

	int num_fields = mysql_num_fields(result);            //结果中域的数量

	MYSQL_FIELD* fields = mysql_fetch_fields(result);     //

	while (MYSQL_ROW row = mysql_fetch_row(result))
	{
		string temp1(row[0]);
		string temp2(row[1]);
		m_users[temp1] = temp2;

		reply = (redisReply*)redisCommand(redis,"SET %s %s",temp1.c_str(),temp2.c_str());
		freeReplyObject(reply);
	}

	return true;
}

/*
*
* 初始化http连接对象
* 
*/
bool http_conn::init(int sockfd, const sockaddr_in& addr, char* root, int trig_mode, 
		  int close_log, string user, string password, string sqlname)
{
	m_sockfd = sockfd;      //设置连接描述符
	m_address = addr;       //设置连接地址结构

	Utils::addfd(Utils::get_epollfd(),sockfd,true,trig_mode);   //这里要设置epolloneshot模式，确保一个socket同一时刻只有一个线程在处理
	
	m_user_count++;            //http连接数量加一

	doc_root = root;           //设置根目录
	m_trig_mode = trig_mode;   //设置连接的触发模式
	m_close_log = close_log;

	memset(sql_user,0,sizeof(sql_user));
	memset(sql_passwd, 0, sizeof(sql_passwd));
	memset(sql_name, 0, sizeof(sql_name));

	strncpy(sql_user,user.c_str(),user.size());
	strncpy(sql_name,sqlname.c_str(),sqlname.size());
	strncpy(sql_passwd,password.c_str(),password.size());

	init();
}

/*
* 
* 初始化新建立的连接
* 
**/
void http_conn::init()
{
	bytes_have_send = 0;
	bytes_to_send = 0;
	m_check_state = CHECK_STATE_REQUESTLINE;      //主状态机的初始状态，解析请求行状态

	m_file_address = NULL;           
	m_linger = false;                //默认不采用长连接
	m_method = GET;
	m_url = NULL;
	m_version = NULL;
	m_content_length = 0;
	m_host = NULL;
	m_start_line = 0;
	m_checked_idx = 0;
	m_read_idx = 0;
	m_write_idx = 0;
	cgi = 0;

	m_state = 0;
	timer_flag = 0;
	improv = 0;

	memset(m_read_buf,0,sizeof(m_read_buf));
	memset(m_write_buf,0,sizeof(m_write_buf));
	memset(m_real_file,0,sizeof(m_real_file));
}


/*
* 
* 从状态机，读取一行数据
*
* m_read_buf         m_checked_idx                m_read_idx           READ_BUFFER_SIZE
*      |                   |                           |                    |
*      \/                  \/                          \/                   \/
*      |********************************************************************|
* 
*/
http_conn::LINE_STATUS  http_conn::parse_line()
{
	char temp;
	for (; m_checked_idx < m_read_idx; ++m_checked_idx)
	{
		temp = m_read_buf[m_checked_idx];
		if (temp == '\r')
		{
			if ((m_checked_idx + 1) == m_read_idx)       
				return LINE_OPEN;
			else if (m_read_buf[m_checked_idx + 1] == '\n')
			{
				m_read_buf[m_checked_idx++] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			
			return LINE_BAD;
		}
		else if (temp == '\n')
		{
			if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r'))
			{
				m_read_buf[m_checked_idx - 1] = '\0';
				m_read_buf[m_checked_idx++] = '\0';

				return LINE_OK;
			}

			return LINE_BAD;
		}
	}

	return LINE_OPEN;    //行不完整
}

/*
*
*输入是一行，以'\0'结尾,解析请求行
*
* 请求行格式： GET  /index.html  HTTP/1.1 
* 
*/
http_conn::HTTP_CODE  http_conn::parse_request_line(char* text)
{
	m_url = strpbrk(text," \t");     //在text中寻找第一个" \t"中字符的位置
	if (NULL == m_url)
	{
		return BAD_REQUEST;         //请求行格式不对
	}

	*m_url++ = '\0';
	char* method = text;
	if (strcasecmp(method,"GET") == 0)   //判断请求方法
	{
		m_method = GET;
	}
	else if (strcasecmp(method,"POST") == 0)
	{
		m_method = POST;
		cgi = 1;
	}
	else
		return BAD_REQUEST;

	m_url += strspn(m_url," \t");      //滤掉空格，指向真正的url
	m_version = strpbrk(m_url," \t");
	if (NULL == m_version)
	{
		return BAD_REQUEST;
	}
	*m_version++ = '\0';
	m_version += strspn(m_version," \t");
	
	if (strcasecmp(m_version,"HTTP/1.1") != 0)
	{
		return BAD_REQUEST;
	}
	if (strncasecmp(m_url,"http://",7) == 0)   //无符号比较，限定数量
	{
		m_url += 7;
		m_url = strchr(m_url,'/');   //跳过主机域名
	}

	if (strncasecmp(m_url,"https://",8) == 0)
	{
		m_url += 8;
		m_url = strchr(m_url,'/');   //跳过主机域名
	}

	if (!m_url || m_url[0] != '/')
	{
		return BAD_REQUEST;
	}

	if (strlen(m_url) == 1)           //访问根目录
	{
		strcat(m_url,"judge.html");   //默认页面,有隐患
	}

	m_check_state = CHECK_STATE_HEADER;   //主状态机状态变换
	return NO_REQUEST;                    //请求不完整，还需要获取数据
}

/*
* 
* 处理完请求行之后，处理请求头部
* 
*/
http_conn::HTTP_CODE  http_conn::parse_headers(char* text)
{
	if (text[0] == '\0')
	{
		if (m_content_length != 0)
		{
			m_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}

		return GET_REQUEST;
	}
	else if (strncasecmp(text,"Connection:",11) == 0)
	{
		text += 11;
		text += strspn(text," \t");     //滤过空白符
		if (strcasecmp(text,"keep-alive") == 0)
		{
			m_linger = true;        //开启长连接
		}
	}
	else if (strncasecmp(text,"Content-length:",15) == 0)
	{
		text += 15;
		text += strspn(text," \t");   //跳过空白符号
		m_content_length = atol(text);
	}
	else if (strncasecmp(text,"Host:",5) == 0)
	{
		text += 5;
		text += strspn(text, " \t");
		m_host = text;
	}
	else
	{
		LOG_ERROR("unknown headers\n");
	}

	return NO_REQUEST;
}


/*
* 
* 生成http响应报文
* 
*/
http_conn::HTTP_CODE  http_conn::do_request()
{
	strcpy(m_real_file,doc_root);        //html文件根目录
	int len = strlen(doc_root);
	const char* p = strrchr(m_url,'/');

	if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) //2是登录，3是注册
	{
		char name[100] = {0};          //用户名
		char passwd[100] = {0};        //密码
		int i = 5;
		int j = 0;

		char m_url_real[200] = {0};
		strcpy(m_url_real,"/");
		strcat(m_url_real,m_url + 2);
		strncpy(m_real_file + len,m_url_real,strlen(m_url_real));        //组成html文件的全部绝对路径名
		m_real_file[len + strlen(m_url_real)] = '\0';

		for (i = 5; m_string[i] != '&'; ++i)
		{
			name[i - 5] = m_string[i];
		}
		name[i - 5] = '\0';

		j = 0;
		for (i = i + 10; m_string[i] != '\0'; ++i)
		{
			passwd[j++] = m_string[i];
		}
		passwd[j] = '\0';

		/*注册*/
		if (*(p + 1) == '3')
		{
			char* sql_insert = (char*)malloc(sizeof(char) * 200);
			memset(sql_insert,0,sizeof(sql_insert));

			/*拼接SQL语句*/
			strcpy(sql_insert,"INSERT INTO user(username,passwd) VALUES(");
			strcat(sql_insert,"'");
			strcat(sql_insert,name);
			strcat(sql_insert,"', '");
			strcat(sql_insert,passwd);
			strcat(sql_insert,"')");

			if (http_conn::m_users.find(name) == http_conn::m_users.end())
			{
				int res = mysql_query(mysql,sql_insert);

				if (!res)
				{
					strcpy(m_url,"/log.html");
					http_conn::m_users.insert(pair<string, string>(name, passwd));    //将用户密码缓存在内存中

					redisContext* redis = NULL;
					redisReply* reply = NULL;

					RedisConnRAII redis_conn(&redis, RedisConnectionPool::GetInstance());
					reply = (redisReply*)redisCommand(redis,"SET %s %s",name,passwd);
					freeReplyObject(reply);
				}
				else
				{
					strcpy(m_url,"/registerError.html");
					LOG_ERROR("resigter error, execute mysql_query error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
				}
			}
			else
			{
				strcpy(m_url,"/registerError.html");
				LOG_ERROR("user %s already in m_user map, func:%s, line:%d\n",__FUNCTION__,__LINE__);
			}
		}
		/*登录*/
		else if(*(p + 1) == '2')
		{
			if (http_conn::m_users.find(name) != http_conn::m_users.end() && http_conn::m_users[name] == passwd)
			{
				strcpy(m_url,"/welcome.html");
			}
			else
			{
				strcpy(m_url,"/logError.html");
				LOG_ERROR("login error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
			}
		}

	}

	if (*(p + 1) == '0')
	{
		strncpy(m_real_file + len,"/register.html",strlen("/register.html"));
		m_real_file[len + strlen("/register.html")] = '\0';
	}
	else if(*(p + 1) == '1')
	{
		strncpy(m_real_file + len, "/log.html",strlen("/log.html"));
		m_real_file[len + strlen("/log.html")] = '\0';
	}
	else if (*(p + 1) == '5')
	{
		strncpy(m_real_file + len, "/picture.html", strlen("/picture.html"));
		m_real_file[len + strlen("/picture.html")] = '\0';
	}
	else if (*(p + 1) == '6')
	{
		strncpy(m_real_file + len, "/video.html", strlen("/video.html"));
		m_real_file[len + strlen("/video.html")] = '\0';
	}
	else if (*(p + 1) == '7')
	{
		strncpy(m_real_file + len, "/fans.html", strlen("/fans.html"));
		m_real_file[len + strlen("/fans.html")] = '\0';
	}
	else
	{
		strncpy(m_real_file + len,m_url,strlen(m_url));
		m_real_file[len + strlen(m_url)] = '\0';
	}

	/*读取文件信息*/
	if (stat(m_real_file, &m_file_stat) < 0)
	{
		LOG_ERROR("file %s no exists, func:%s, line:%d\n",m_real_file,__FUNCTION__,__LINE__);
		return NO_RESUORCE;     //资源不存在
	}

	/*判断文件是否有权限读取*/
	if (!(m_file_stat.st_mode & S_IROTH))
	{
		LOG_ERROR("file %s have not permit, func:%s, line:%d\n", m_real_file, __FUNCTION__, __LINE__);
		return FORBIDDEN_REQUEST;    //没有权限，请求资源禁止访问
	}

	/*判断文件是否是目录*/
	if (S_ISDIR(m_file_stat.st_mode))
	{
		LOG_ERROR("file %s is dir, func:%s, line:%d\n",m_real_file,__FUNCTION__,__LINE__);
		return BAD_REQUEST;     
	}

	//可读方式打开文件
	int fd = open(m_real_file,O_RDONLY);      
	//将文件内容映射到内存，提高读取速度
	m_file_address = (char *)mmap(NULL,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
	//关闭文件
	close(fd);

	return FILE_REQUEST;      //响应正确的html文件
}

/*
* 
* 释放加速读写文件所映射的内存
* 
*/
void http_conn::unmap()
{
	if (m_file_address)
	{
		munmap(m_file_address,m_file_stat.st_size);   //释放映射的内存空间
		m_file_address = NULL;
	}
}

/*
*
* 收到http报文数据之后，进行处理；（注意，收到的可能不是一个完整的http报文)
*
* 返回值:
* NO_REQUEST   --> http报文不完整
* BAD_REQUEST  --> 到目前为止，http报文的请求中出现了错误
* GET_REQUEST  --> 解析完了完整的http请求报文
* INTERNAL_ERROR  --> 内部错误
*/
http_conn::HTTP_CODE http_conn::process_read()
{
	LINE_STATUS line_status = LINE_OK;      //从状态机的状态,为了请求体数据一次没有全部收到时，下次收到时，能够再次进入循环进行解析
	HTTP_CODE ret = NO_REQUEST;
	char* text = NULL;

	/*完整的读取到一行*/
	/*或的第一个条件是为了处理请求体*/
	while ((line_status == LINE_OK && m_check_state == CHECK_STATE_CONTENT) || ((line_status = parse_line()) == LINE_OK))      //parse_line是从状态机
	{
		text = get_line();                 //获取一行数据
		
		LOG_INFO("get one line:%s\n",text);

		m_start_line = m_checked_idx;      //刷新行的起始位置

		switch (m_check_state)             //主状态机的状态
		{
			case CHECK_STATE_REQUESTLINE:  //主状态机状态是请求行
			{
				ret = parse_request_line(text);  //解析请求行，里面会修改m_chech_state的状态，来驱动主状态机
				if (ret == BAD_REQUEST)          //http请求的语法有错
				{
					return BAD_REQUEST;          
				}
				break;
			}
			case CHECK_STATE_HEADER:
			{
				ret = parse_headers(text);
				if (ret == BAD_REQUEST)         //http请求的语法有错
				{
					return BAD_REQUEST;
				}

				/*有可能没有请求体，比如GET请求*/
				if (ret == GET_REQUEST)         //获得了完整的http请求全部数据
				{
					return do_request();
				}

				break;
			}
			case CHECK_STATE_CONTENT:
			{
				ret = parse_content(text);
				if (ret == GET_REQUEST)        //完整获取到了http请求
				{
					return do_request();
				}

				line_status = LINE_OPEN;       //未完全解析请求体（因为还没完整的收到），这样可以用来退出整个while循环
				break;
			}
			default:
			{
				return INTERNAL_ERROR;          //出错
			}
		}
	}

	return NO_REQUEST;       //请求不完整
}

/*
* 
* 解析http的请求体内容
* 
* 
*/
http_conn::HTTP_CODE  http_conn::parse_content(char* text)
{
	/*http_conn的接收缓冲区中是否已经拿到了全部的请求体部分的数据*/
	if (m_read_idx >= (m_checked_idx + m_content_length))
	{
		text[m_content_length] = '\0';
		m_string = text;
		return GET_REQUEST;    //http请求已经完整获取到了
	}

	/*请求体的数据不全*/
	return NO_REQUEST;
}

/*
* 
* 
*/
void removefd(int epollfd,int fd)
{
	epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
	close(fd);
}

/*
* 
* 在http响应过程中出错，直接关闭连接
* 
*/
bool http_conn::close_conn(bool real_close)
{
	if (real_close && (m_sockfd != -1))
	{
		LOG_ERROR("m_sockfd %d will be closed, func:%s, line:%d\n",m_sockfd,__FUNCTION__,__LINE__);
		removefd(m_epollfd,m_sockfd);
		m_sockfd = -1;
		locker.lock();
		m_user_count--;
		locker.unlock();
	}

	return true;
}

/*
* 
* 处理函数
* 
*/
void http_conn::process()
{
	HTTP_CODE read_ret = process_read();      //处理暂时收到的http数据



	/*请求不完整，还需要继续获取数据*/
	if (read_ret == NO_REQUEST)
	{
		Utils::modfd(m_epollfd,m_sockfd,EPOLLIN,m_trig_mode);    //重置epolloneshot事件(需要继续读),让epoll能再次触发读事件
		return;    //直接返回
	}

	bool write_ret = process_write(read_ret);              //根据http报文的分析情况构造http响应数据
	if (!write_ret)
	{
		close_conn();         //构造响应有问题，关闭连接
	}
	
	Utils::modfd(m_epollfd,m_sockfd,EPOLLOUT,m_trig_mode); //让监听可写事件，也是epolloneshot事件
}

/*
* 
* 可变参数添加响应
* 
*/
bool http_conn::add_response(const char* format, ...)
{
	/*判断写缓冲区是否已经满*/
	if (m_write_idx >= WRITE_BUFFER_SIZE)
	{
		LOG_ERROR("m_write_idx >= WRITE_BUFFER_SIZE, func:%s, line:%d\n");
		return false;
	}

	va_list arg_list;
	va_start(arg_list,format);
	int len = vsnprintf(m_write_buf + m_write_idx,WRITE_BUFFER_SIZE - 1 - m_write_idx,format,arg_list);

	/*判断写缓冲区是否已经满*/
	if (len >= WRITE_BUFFER_SIZE - m_write_idx - 1)
	{
		va_end(arg_list);
		return false;
	}

	m_write_idx += len;

	va_end(arg_list);

	return true;
}

/*
* 
* 添加状态行
* 
* HTTP/1.1 200 OK
*/
bool http_conn::add_status_line(int status, const char* title)
{
	return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

/*
* 
* 添加头部
* 
*/
bool http_conn::add_headers(int content_length)
{
	return add_content_length(content_length) && add_linger() && add_blank_line();
}

/*
* 
* 添加Content-Length 响应头部
* 
*/
bool http_conn::add_content_length(int content_length)
{
	return add_response("Content-Length:%d\r\n",content_length);
}

/*
* 
* 添加Content-Type 响应头部
* 
*/
bool http_conn::add_content_type()
{
	return add_response("Content-Type:%s\r\n","text/html");
}

/*
* 
* 添加keep-alive 头部
* 
*/
bool http_conn::add_linger()
{
	return add_response("Connection:%s\r\n",m_linger ? "keep-alive" : "close");
}

/*
* 
* 添加空白行，响应头部和响应体之间的
* 
*/
bool http_conn::add_blank_line()
{
	return add_response("%s","\r\n");
}

/*
* 
* 添加响应体内容
* 
*/
bool http_conn::add_content(const char* content)
{
	return add_response("%s",content);
}


/*
* 
* 根据http报文的分析处理结果构建响应报文
* 
* ret 是http请求报文处理之后的结果值
* 
*/
bool http_conn::process_write(http_conn::HTTP_CODE ret)
{
	switch (ret)
	{
		case INTERNAL_ERROR:
		{
			add_status_line(500,error_500_title);
			add_headers(strlen(error_500_form));
			if (!add_content(error_500_form))
			{
				return false;
			}
			break;
		}
		case BAD_REQUEST:
		{
			add_status_line(400,error_400_title);
			add_headers(strlen(error_400_form));
			if (!add_content(error_400_form))
			{
				return false;
			}
			break;
		}
		case FORBIDDEN_REQUEST:
		{
			add_status_line(403,error_403_title);
			add_headers(strlen(error_403_form));
			if (!add_content(error_403_form))
			{
				return false;
			}
			break;
		}
		case NO_RESUORCE:
		{
			add_status_line(404, error_404_title);
			add_headers(strlen(error_404_form));
			if (!add_content(error_404_form))
			{
				return false;
			}
			break;
		}
		case FILE_REQUEST:    //文件请求
		{
			add_status_line(200,ok_200_title);
			if (m_file_stat.st_size != 0)
			{
				add_headers(m_file_stat.st_size);
				m_iv[0].iov_base = m_write_buf;
				m_iv[0].iov_len = m_write_idx;
				m_iv[1].iov_base = m_file_address;
				m_iv[1].iov_len = m_file_stat.st_size;
				m_iv_count = 2;
				bytes_to_send = m_write_idx + m_file_stat.st_size;

				return true;
			}
			else     //如果html响应文件是空的
			{
				const char* ok_string = "<html><body></body></html>";
				add_headers(strlen(ok_string));
				if (!add_content(ok_string))
				{
					return false;
				}
			}
			
		}
		
		default:
		{
			LOG_ERROR("unknown ret, func:%s, line:%d\n");
			return false;
		}
	}

	m_iv[0].iov_base = m_write_buf;
	m_iv[0].iov_len = m_write_idx;
	m_iv_count = 1;
	bytes_to_send = m_write_idx;
	return true;
}

/*
* 
* 将http_conn的发送缓冲区中的内容发送出去
* 
*/
bool http_conn::write()
{
	int temp = 0;

	if (bytes_to_send == 0)//没有字节要发送
	{
		Utils::modfd(m_epollfd,m_sockfd,EPOLLIN,m_trig_mode);   //重新注册epolloneshot读事件
		init();        //刷新http_conn对象的状态
		return true;
	}

	while (true)
	{
		temp = writev(m_sockfd,m_iv,m_iv_count);         //集中写

		if (temp < 0)
		{
			if (errno == EAGAIN)         //非阻塞模式,并不是出错，而是写缓冲区已满，writev系统调用立即返回了
			{
				Utils::modfd(m_epollfd,m_sockfd,EPOLLOUT,m_trig_mode);   //重置epolloneshot写事件
				return true;
			}

			//writev错误
			unmap();
			return false;
		}

		bytes_have_send += temp;
		bytes_to_send -= temp;

		if (bytes_have_send >= m_iv[0].iov_len)
		{
			/*更新要发送的字节数组*/
			m_iv[0].iov_len = 0;
			m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
			m_iv[1].iov_len = bytes_to_send;
		}
		else
		{
			m_iv[0].iov_base = m_write_buf + bytes_have_send;
			m_iv[0].iov_len = m_write_idx - bytes_have_send;
		}

		/*数据已经全部发送完*/
		if (bytes_to_send <= 0)
		{
			unmap();
			Utils::modfd(m_epollfd,m_sockfd,EPOLLIN,m_trig_mode);      //响应数据已经全部发送完，所以重置epolloneshot读事件

			/*是否长连接,keep-alive*/
			if (m_linger)
			{
				init();
				return true;
			}
			else
			{
				return false;
			}
		}
	}
}

/*
* 
* 读取到达的请求数据，如果是ET触发模式，则应该一次性全部读取
* 
*/
bool http_conn::read_once()
{
	/**/
	if (m_read_idx >= READ_BUFFER_SIZE)
	{
		return false;     //读失败,缓冲区满了
	}

	int bytes_read = 0;

	/*根据触发模式读*/
	if (m_trig_mode == LT)
	{
		bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx,0);

		if (bytes_read < 0)
		{
			return false;
		}

		m_read_idx += bytes_read;
	}
	else if(m_trig_mode == ET)
	{
		while (true)
		{
			bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
			
			if (bytes_read == -1)   
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK)  //数据已经一次性全部读完了
				{
					break;
				}
				
				return false;       //读有问题
			}
			else if (bytes_read == 0)  //断开
			{
				return false;
			}

			m_read_idx += bytes_read;
			if (m_read_idx >= READ_BUFFER_SIZE)    //读缓冲区溢出
			{
				return false;
			}
		}
	}
	else
	{
		return false;
	}

	return true;
}
