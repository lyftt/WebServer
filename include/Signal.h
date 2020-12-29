#ifndef __SIGNAL_H__
#define __SIGNAL_H__

/*
* 
*信号处理类 
* 
*/
class Signal
{
public:
	//信号处理函数
	static void sig_handler(int sig);

	//设置信号处理函数
	static bool addsig(int sig, void (handler)(int), bool restart = true);

	//设置统一事件源的管道
	static void set_pipfd(int *pipefd);

private:
	static int* m_pipfd;     //统一事件源管道
};

#endif