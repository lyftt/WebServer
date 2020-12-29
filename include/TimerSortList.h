#ifndef __TIMER_SORT_LIST_H__
#define __TIMER_SORT_LIST_H__

#include "Timer.h"

/*
*
*实现TimerBase接口
* 
*/
class TimerSortList : public TimerBase
{
public:
	TimerSortList();
	~TimerSortList();

	/*添加定时器*/
	virtual bool add_timer(TimerUnit* timer) override;

	/*调整定时器*/
	virtual bool adjust_timer(TimerUnit* timer) override;

	/*删除定时器*/
	virtual bool del_timer(TimerUnit* timer) override;

	/*滴答函数*/
	virtual void tick() override;

private:

	bool add_timer(TimerUnit* timer, TimerUnit* lst_head);

	int m_size;
	TimerUnit* m_head;
	TimerUnit* m_tail;
};


#endif