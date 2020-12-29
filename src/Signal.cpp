#include "Signal.h"
#include "log.h"
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <string.h>
#include <sys/epoll.h>

//静态变量定义
int* Signal::m_pipfd = NULL;

/*
* 
* 将被安装的信号处理函数
* 
*/
void Signal::sig_handler(int sig)
{
	//保证函数的可重入性，保留进入之前的errno错误码
	int save_errno = errno;
	int msg = sig;
	send(m_pipfd[1],(char*)&msg,1,0);      //发送收到的信号,管道1用来写
	errno = save_errno;
}

/*
*
* 添加信号处理函数
* 
*/
bool Signal::addsig(int sig, void (handler)(int), bool restart)
{
	struct sigaction sa;
	memset(&sa,0,sizeof(sa));
	sa.sa_handler = handler;       //设置信号处理函数

	if (restart)
		sa.sa_flags |= SA_RESTART;       //阻塞的系统调用可能被中断，自动重启被中断的系统调用

	sigfillset(&sa.sa_mask);             //当执行我们设置的信号处理函数时，阻塞所有信号

	int ret = sigaction(sig,&sa,NULL);
	if (ret < 0)
	{
		LOG_ERROR("sigaction error, func:%s, line:%d \n", __FUNCTION__, __LINE__);
		return false;
	}

	return true;
}

/*
*设置统一事件源管道
*
*/
void Signal::set_pipfd(int *pipefd)
{
	Signal::m_pipfd = pipefd;
}