#include "webserver.h"
#include "log.h"
#include "config.h"
#include "Utils.h"
#include "Signal.h"
#include <string>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>


/*
*
* WebServer的构造函数
* 
*/
WebServer::WebServer():m_root(NULL),users(NULL),m_pool(NULL),users_timer(NULL)
{
	//http_conn对象数组，一个连接对应一个
	users = new (std::nothrow) http_conn[MAX_FD];        //WebServer的http连接数组，一个连接对应一个文件描述符

	//root文件夹路径
	char server_path[200];
	memset(server_path,0,sizeof(server_path));
	getcwd(server_path, sizeof(server_path));
	char root[6] = "/root";
	m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
	strcpy(m_root,server_path);
	strcat(m_root,root);       //将root追加到m_root结尾

	//定时器数组，也是一个连接对应一个
	users_timer = new client_data[MAX_FD];              //将用户数据(连接文件描述符、连接地址)和定时器关联起来 
}

WebServer::~WebServer()
{
	close(m_epollfd);         //关闭epoll，需要通过close来关闭epoll_create创建的epollfd
	close(m_listenfd);        //关闭监听描述符
	close(m_pipefd[0]);       //关闭统一事件源的管道
	close(m_pipefd[1]);       //关闭统一事件源的管道

	/*释放线程池资源*/
	if (m_pool)
	{
		delete m_pool;
		m_pool = NULL;
	}

	/*释放http_conn数组资源*/
	if (users)
	{
		delete [] users;
		users = NULL;
	}

	/*释放定时器资源*/
	if (users_timer)
	{
		delete [] users_timer;
		users_timer = NULL;
	}

	/*释放m_root*/
	if (m_root)
	{
		free(m_root);
		m_root = NULL;
	}

	/*销毁redis连接池*/
	if (m_redis_pool)
	{
		RedisConnectionPool::RevInstance();
		m_redis_pool = NULL;
	}

	/*销毁数据库连接池*/
	if (m_conn_pool)
	{
		SqlConnectionPool::RevInstance();
		m_conn_pool = NULL;
	}
}


/*
* 
* WebServer的初始化
* 
*/
void WebServer::init(int port, string user, string pass_word, string database_name,
					 int log_write, int opt_linger, int trig_mode, int sql_num,
					 int thread_num, int close_log, int actor_mode,int redis_num)
{
	m_port = port;           //设置WebServer的服务端口
	m_user = user;           //设置用户
	m_password = pass_word;  //设置用户登录密码
	m_database_name = database_name;   //设置使用的Mysql数据库的名字
	m_log_write = log_write;           //设置日志的写入方式，异步/同步，0同步，1异步
	m_sql_num = sql_num;               //设置数据库连接池的连接数量
	m_opt_linger = opt_linger;         //设置是否优雅关闭连接
	m_trig_mode = trig_mode;           //设置触发模式
	m_thread_num = thread_num;         //设置线程池线程数量
	m_close_log = close_log;           //设置日志是否关闭
	m_actor_model = actor_mode;        //设置并发模型，reactor/proactor
	m_redis_num = redis_num;           //设置redis连接的数量
}


/*
*
* WebServer的日志部分初始化
* 
* 
*/
bool WebServer::log_init()
{
	/*是否关闭日志*/
	if (LOG_ON == m_close_log)
	{
		//根据日志的写入方式,初始化日志类，1为异步，0为同步
		if (LOG_ASYNS == m_log_write)
		{
			if (!Log::get_instance()->init("./server_log", 2000, 800000, 800))       //异步日志
			{
				std::cout << "log_init() async error" << std::endl;
				return false;
			}
		}
		else
		{
			if (!Log::get_instance()->init("./server_log", 2000, 80000, 0))          //同步日志
			{
				std::cout << "log_init() sync error" << std::endl;
				return false;
			}
		}

		LOG_START();
	}
	//关闭日志
	else
	{
		LOG_STOP();
	}

	return true;
}

/*
* 
* redis池初始化
* 
*/
bool WebServer::redis_pool_init()
{
	m_redis_pool = RedisConnectionPool::GetInstance();
	if (!m_redis_pool)
	{
		LOG_ERROR("RedisConnectionPool::GetInstance() error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
		return false;
	}

	if (!m_redis_pool->init("localhost",6379,m_redis_num))
	{
		LOG_ERROR("m_redis_pool->init() error, func:%s, line:%d\n", __FUNCTION__, __LINE__);
		return false;
	}
	return true;
}

/*
* 
* 数据库连接池初始化
* 
*/
bool WebServer::sql_pool_init()
{
	m_conn_pool = SqlConnectionPool::GetInstance();
	if (!m_conn_pool)
	{
		LOG_ERROR("SqlConnectionPool::GetInstance() error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
		return false;
	}

	if (!m_conn_pool->init("localhost", m_user, m_password, m_database_name, 3306, m_sql_num))
	{
		LOG_ERROR("m_conn_pool->init() error, func:%s, line:%d\n", __FUNCTION__,__LINE__);
		return false;
	}

	return users->init_mysql_result(m_conn_pool,m_redis_pool);      //将Mysql中的所有登录数据加载到内存
}

/*
*
* WebServer 触发模式的初始化
* 
*/
bool WebServer::trig_mode()
{
	//LT + LT
	if (LT_LT == m_trig_mode)
	{
		m_listen_trig_mode = LT;       //监听套接字的触发模式,0 -> LT
		m_conn_trig_mode = ET;         //连接套接字的触发模式,0 -> LT
	}
	//LT + ET
	else if (LT_ET == m_trig_mode)
	{
		m_listen_trig_mode = LT;       //LT
		m_conn_trig_mode = ET;         //ET
	}
	//ET + LT
	else if (ET_LT == m_trig_mode)
	{
		m_listen_trig_mode = ET;       //ET
		m_conn_trig_mode = LT;         //LT
	}
	//ET + ET
	else if (ET_ET == m_trig_mode)
	{
		m_listen_trig_mode = ET;       //ET
		m_conn_trig_mode = ET;         //ET
	}

	return true;
}


/*
*
* WebServer线程池初始化
* 
*/
bool WebServer::thread_pool_init()
{
	m_pool = new (std::nothrow) ThreadPool<http_conn>(m_actor_model, m_conn_pool, m_thread_num, 10000);   //不抛出异常

	if (!m_pool)
	{
		std::cout << "new ThreadPool error" << std::endl;
		return false;
	}

	return true;
}

/*
* 
* 给新到的连接添加定时器，初始化http连接对象
* 
**/
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
	/*初始化一个http连接对象*/
	users[connfd].init(connfd,client_address,m_root,m_conn_trig_mode,m_close_log,m_user,m_password,m_database_name);

	/*为http连接添加定时器*/
	users_timer[connfd].address = client_address;
	users_timer[connfd].sockfd = connfd;
	TimerUnit* timer_tmp = new TimerUnit;
	timer_tmp->user_data = &users_timer[connfd];
	timer_tmp->cb_func = Utils::cb_func;          //设置回调函数
	time_t cur = time(NULL);
	timer_tmp->expire = cur + 3 * TIMESLOT;       //设置超时时间
	users_timer[connfd].timer = timer_tmp;
	sort_timer_list.add_timer(timer_tmp);         //添加定时器单元到升序链表
}


/*
* 
* 调整TCP连接关联的定时器，刷新到期时间
* 
**/
bool WebServer::adjust_timer(TimerUnit* timer)
{
	time_t cur_time = time(NULL);
	timer->expire = cur_time + 3 * TIMESLOT;    //刷新定时器到期时间

	sort_timer_list.adjust_timer(timer);        //升序链表中进行调整
	LOG_INFO("adjust timer once, func:%s, line:%d\n",__FUNCTION__,__LINE__);
}

/*
*
* 删除TCP连接关联的定时器
*
**/
bool WebServer::del_timer(TimerUnit* timer, int sockfd)
{
	if (!timer)
		return false;

	timer->cb_func(timer->user_data);       //执行回调函数，关闭连接，删除红黑树上的监听事件,http连接对象减一
	sort_timer_list.del_timer(timer);       //升序链表删除相关定时器

	LOG_INFO("close fd: %d\n",sockfd);
}

/*
* 
* 处理新到的连接
* 
**/
bool WebServer::deal_client_data()
{
	struct sockaddr_in client_address;         //对端的TCP连接地址结构
	socklen_t len = sizeof(client_address);
	memset(&client_address,0,sizeof(client_address));

	/*判断监听描述符的触发模式，LT 或 ET 模式*/
	if (m_listen_trig_mode == LT)
	{
		int connfd = accept(m_listenfd,(struct sockaddr *)&client_address,&len);   //得到连接描述符

		if (connfd < 0)
		{
			LOG_ERROR("accept error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
			return false;
		}

		if (http_conn::m_user_count >= MAX_FD) //如果http连接的数量已经满了
		{
			Utils::show_error(connfd,"Internal server busy");
			LOG_ERROR("m_user_count is full, func:%s, line:%d\n",__FUNCTION__,__LINE__);
			return false;
		}
		
		LOG_INFO("accept new tcp\n");
		timer(connfd,client_address);  //添加新TCP连接到定时器，并出示http_conn对象
	}
	//ET模式，要一次性取够
	else if(m_listen_trig_mode == ET)
	{
		while (1)
		{
			int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &len);   //得到连接描述符

			if (connfd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			{
				LOG_INFO("already process comming connfd\\n");
				break;
			}
			else
			{
				Utils::show_error(connfd, "Internal server busy");
				LOG_ERROR("accept error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
				return false;
			}

			if (http_conn::m_user_count >= MAX_FD)
			{
				Utils::show_error(connfd, "Internal server busy");
				LOG_ERROR("m_user_count is full, func:%s, line:%d\n", __FUNCTION__, __LINE__);
				return false;
			}

			timer(connfd,client_address);  //添加新TCP连接的定时器
		}
	}
	else    //监听套接字的触发方式有问题
	{
		return false;
	}

	return true;
}

/*
*
* 处理信号事件
* 输入：
* timeout
* stop_server
**/
bool WebServer::deal_with_signal(bool& timeout, bool& stop_server)
{
	int ret = 0;
	char signals[1024];
	memset(signals,0,sizeof(signals));

	ret = recv(m_pipefd[0],signals,sizeof(signals),0);
	if (ret < 0)
	{
		LOG_ERROR("recv error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
		return false;
	}
	else if (0 == ret)
	{
		LOG_ERROR("recv zero, peer disconnect, func:%s, line:%d\n",__FUNCTION__,__LINE__);
		return false;
	}
	else
	{
		for (int i = 0; i < ret; ++i)
		{
			switch (signals[i])
			{
				case SIGALRM:
				{
					timeout = true;
					LOG_INFO("Get Signal SIGALARM, func:%s, line:%d\n",__FUNCTION__,__LINE__);
					break;
				}
				case SIGTERM:
				{
					stop_server = true;
					LOG_INFO("Get Signal SIGTERM, func:%s, line:%d\n", __FUNCTION__, __LINE__);
					break;
				}
				default:
				{
					LOG_INFO("Get Signal Other, func:%s, line:%d\n", __FUNCTION__, __LINE__);
					break;
				}
			}
		}
	}

	return true;
}

/*
*
* 处理TCP连接上的可读事件
* sockfd描述符可读，有数据到达
**/
bool WebServer::deal_with_read(int sockfd)
{
	//获取sockfd相关的定时器
	TimerUnit* timer = users_timer[sockfd].timer;
	if (!timer)
	{
		LOG_ERROR("timer is null\n, func:%s, line:%s\n",__FUNCTION__,__LINE__);
		return false;
	}

	/*reactor反应堆模型，读写都由工作线程来完成*/
	if (m_actor_model == RE_ACTOR)
	{
		adjust_timer(timer);

		m_pool->append(users + sockfd,0);     //将http对象的可读事件放入线程池，0表示读

		while (true)
		{
			if (1 == users[sockfd].improv)           //读操作结束
			{
				if (1 == users[sockfd].timer_flag)   //但是读数据有问题
				{
					del_timer(timer,sockfd);         //关闭连接，删除定时器
					users[sockfd].improv = 0;
				}

				users[sockfd].improv = 0;
				break;
			}
		}
	}
	/*proactor反应堆模型，读写都由主线程解决，工作线程只负责处理*/
	else if (m_actor_model == PRO_ACTOR)
	{
		/*主线程负责读取数据，交给线程池中的工作线程处理*/
		if (users[sockfd].read_once())
		{
			m_pool->append_p(users + sockfd);   //读到了数据，直接放入线程池，让工作线程处理

			adjust_timer(timer);                //调整定时器,刷新时间
		}
		/*读数据失败*/
		else
		{
			LOG_ERROR("pthread id:%d, PROACTOR read error, func:%s, line:%d\n",syscall(224),__FUNCTION__,__LINE__);
			del_timer(timer,sockfd);
		}
	}
	else
	{
		LOG_ERROR("pthread id:%d, unknown actor model, func:%s, line:%d\n",syscall(224),__FUNCTION__,__LINE__);
		return false;
	}

	return true;
}

/*
*
* 处理TCP连接上的可写事件
*
**/
bool WebServer::deal_with_write(int sockfd)
{
	TimerUnit* timer = users_timer[sockfd].timer;
	if (!timer)
	{
		LOG_ERROR("timer is null\n, func:%s, line:%s\n", __FUNCTION__, __LINE__);
		return false;
	}

	if (m_actor_model == RE_ACTOR)
	{
		adjust_timer(timer);                    //定时器的时间调整

		m_pool->append(users + sockfd,1);       //1表示写

		while (true)
		{
			if (1 == users[sockfd].improv)             //读操作结束
			{
				if (1 == users[sockfd].timer_flag)     //但是写数据有问题
				{
					del_timer(timer, sockfd);          //关闭连接，删除定时器
					users[sockfd].improv = 0;
					LOG_ERROR("pthread id:%d, REACTOR, write error, func:%s, line:%d\n", syscall(224), __FUNCTION__, __LINE__);
				}

				users[sockfd].improv = 0;
				break;
			}
		}
	}
	else if (m_actor_model == PRO_ACTOR)
	{
		/*读写都是主线程负责的*/
		if (users[sockfd].write())
		{
			adjust_timer(timer);    //刷新定时器
		}
		/*发送数据失败*/
		else
		{
			del_timer(timer,sockfd);   //关闭连接，删除定时器
			LOG_ERROR("pthread id:%d, PROACTOR, write error, func:%s, line:%d\n",syscall(224),__FUNCTION__,__LINE__);
		}
	}
	else
	{
		LOG_ERROR("pthread id:%d, unknown actor model, func:%s, line:%d\n",syscall(224),__FUNCTION__,__LINE__);
	}
}


/*
*
* WebServer 事件循环初始化
* 
*/
bool WebServer::event_listen()
{
	m_listenfd = socket(AF_INET,SOCK_STREAM,0);   //监听套接字
	if (m_listenfd < 0)
	{
		LOG_ERROR("socket error, func:%s, line:%d \n",__FUNCTION__,__LINE__);
		return false;
	}

	//普通关闭连接
	if (NORM_CLOSE == m_opt_linger)
	{
		struct linger tmp = {0,1};   
		setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
	}
	//优雅关闭连接
	else if(NICE_CLOSE == m_opt_linger)
	{
		struct linger tmp = {1,1};     //最多等待1s
		setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
	}

	int ret = 0;
	int flag = 1;
	struct sockaddr_in address;
	bzero(&address,sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(m_port);
	address.sin_addr.s_addr = htonl(INADDR_ANY);    //监听所有网卡

	setsockopt(m_listenfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));   //设置端口复用，应对TIME_WAIT
	ret = bind(m_listenfd,(struct sockaddr*)&address,sizeof(address));
	if (ret < 0)
	{
		LOG_ERROR("bind error, func:%s, line:%d \n", __FUNCTION__, __LINE__);
		return false;
	}

	ret = listen(m_listenfd,5);
	if (ret < 0)
	{
		LOG_ERROR("listen error, func:%s, line:%d \n", __FUNCTION__, __LINE__);
		return false;
	}

	//事件数组，用来接收返回的事件数组
	epoll_event events[MAX_EVENT_NUMBER];    
	m_epollfd = epoll_create(5);
	if (ret < 0)
	{
		LOG_ERROR("epoll_create error, func:%s, line:%d \n", __FUNCTION__, __LINE__);
		return false;
	}

	//将监听描述符添加到内核事件表（红黑树）中
	Utils::addfd(m_epollfd,m_listenfd,false,m_listen_trig_mode);   //不开启one_shot，内部含有设置非阻塞操作
	http_conn::m_epollfd = m_epollfd;

	//创建Unix域套接字
	ret = socketpair(AF_UNIX,SOCK_STREAM,0,m_pipefd);
	if (ret < 0)
	{
		LOG_ERROR("socketpair error, func:%s, line:%d \n", __FUNCTION__, __LINE__);
		return false;
	}

	//设置统一事件源管道非阻塞并通过epoll监听
	Utils::setnonblocking(m_pipefd[1]);
	Utils::addfd(m_epollfd,m_pipefd[0],false,0);     //不开启one_shot, 管道0是读，1是写
	
	//设置信号处理
	Signal::set_pipfd(m_pipefd);       //告知信号处理函数统一事件源的管道
	Signal::addsig(SIGPIPE,SIG_IGN);   //SIGPIPE信号的处理需要关注下
	Signal::addsig(SIGALRM,Signal::sig_handler,false);
	Signal::addsig(SIGTERM,Signal::sig_handler,false);

	//配置Utils
	Utils::set_epollfd(m_epollfd);

	//定时
	alarm(TIMESLOT);

	return true;
}


bool WebServer::event_loop()
{
	bool stop_server = false;
	bool timeout = false;

	while (!stop_server)
	{
		/*监听事件的发生*/
		int number = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);   //events里装发生的事件,返回发生的事件的数量
		if (number < 0 && errno != EINTR)       //如果不是被信号打断
		{
			LOG_ERROR("epoll_wait error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
			return false;
		}

		/*遍历所有的发生事件的描述符*/
		for (int i = 0; i < number; ++i)
		{
			int sockfd = events[i].data.fd;   //取出文件描述符
			bool success_flag = false;

			/*如果是有连接需要建立*/
			if (sockfd == m_listenfd)
			{
				success_flag = deal_client_data();        //处理新到的连接，初始化http连接对象，添加定时器
				if (!success_flag)
				{
					LOG_ERROR("deal_client_data error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
					continue;
				}
			}
			/*如果是异常事件，比如对端连接断开*/
			else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
			{
				TimerUnit* timer = users_timer[sockfd].timer;     //找到这个连接关联的定时器
				del_timer(timer,sockfd);                          //删除定时器，并关闭关联的连接
			}
			/*如果是信号*/
			else if (sockfd == m_pipefd[0] && (events[i].events &  EPOLLIN))
			{
				success_flag = deal_with_signal(timeout,stop_server);     //处理信号,包括了定时信号
				if (!success_flag)
				{
					LOG_ERROR("deal_with_signal error, func:%s, line:%d\n",__FUNCTION__,__LINE__);
					continue;
				}
			}
			/*已经建立连接的TCP连接上有数据可读*/
			else if (events[i].events & EPOLLIN)
			{
				success_flag = deal_with_read(sockfd);
				if (!success_flag)
				{
					LOG_ERROR("deal_with_read error, func:%s, line:%d\n", __FUNCTION__, __LINE__);
					continue;
				}
			}
			/*已经建立连接的TCP连接上有数据可写*/
			else if (events[i].events & EPOLLOUT)
			{
				success_flag = deal_with_write(sockfd);
				if (!success_flag)
				{
					LOG_ERROR("deal_with_write error, func:%s, line:%d\n", __FUNCTION__, __LINE__);
					continue;
				}
			}
		}

		if (timeout)
		{
			Utils::timer_handler(&sort_timer_list);

			LOG_INFO("timer tick\n");

			timeout = false;
		}
	}
}

