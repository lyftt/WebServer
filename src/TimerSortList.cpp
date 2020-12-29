#include "TimerSortList.h"

TimerSortList::TimerSortList() :m_head(NULL), m_tail(NULL), m_size(0)
{

}

/*
*
*释放升序链表定时器
*/
TimerSortList::~TimerSortList()
{
	TimerUnit* tmp = m_head;

	while (tmp)
	{
		m_head = tmp->next;
		delete tmp;
		tmp = m_head;
	}
}

/*
*
*添加定时单元
*
*/
bool TimerSortList::add_timer(TimerUnit* timer)
{
	if (!timer)
		return false;

	/*链表空的情况*/
	if (m_size == 0 || !m_head)
	{
		m_head = m_tail = timer;
		m_size++;
		return true;
	}

	if (timer->expire < m_head->expire)
	{
		timer->prev = m_head->prev;
		timer->next = m_head;
		m_head->prev = timer;
		m_head = timer;
		m_size++;
		return true;
	}

	/*处理不是在链表头部的情况*/
	return add_timer(timer, m_head);
}

/*
* 调整定时器单元，只能向后调整
*
*
*/
bool TimerSortList::adjust_timer(TimerUnit* timer)
{
	if (!timer)
	{
		return false;
	}

	TimerUnit* tmp = timer->next;
	if (!tmp || timer->expire <= tmp->expire)
	{
		return true;
	}

	/*如果要调整的是头节点*/
	if (timer == m_head)
	{
		m_head = m_head->next;
		m_head->prev = timer->prev;
		m_size--;
		timer->next = NULL;
		timer->prev = NULL;
		return add_timer(timer, m_head);
	}

	/*如果要调整的不是头节点*/
	timer->prev->next = timer->next;
	timer->next->prev = timer->prev;
	m_size--;

	return add_timer(timer, timer->next);
}

/*
*
* 删除定时器单元
*
*/
bool TimerSortList::del_timer(TimerUnit* timer)
{
	if (!timer)
		return false;

	if (m_size == 0 || !m_head)
		return false;

	/*仅有一个*/
	if ((timer == m_head) && (timer == m_tail))
	{
		delete timer;
		m_head = NULL;
		m_tail = NULL;
		m_size--;
		return true;
	}

	/*头部*/
	if (timer == m_head)
	{
		m_head = m_head->next;
		m_head->prev = timer->prev;
		delete timer;
		m_size--;
		return true;
	}

	/*尾部*/
	if (timer == m_tail)
	{
		m_tail = m_tail->prev;
		m_tail->next = timer->next;
		delete timer;
		m_size--;
		return true;
	}

	timer->prev->next = timer->next;
	timer->next->prev = timer->prev;
	delete timer;
	m_size--;

	return true;
}

/*
*
* 将定时器单元添加到lst_head节点之后的部分链表中
*
*/
bool TimerSortList::add_timer(TimerUnit* timer, TimerUnit* lst_head)
{
	TimerUnit* tmp = lst_head->next;

	while (tmp)
	{
		if (timer->expire < tmp->expire)
		{
			timer->next = tmp;
			timer->prev = tmp->prev;
			tmp->prev->next = timer;
			tmp->prev = timer;
			m_size++;
			return true;
		}
		else
		{
			tmp = tmp->next;
		}
	}

	/*尾部，最后一个元素之后插入*/
	if (!tmp)
	{
		timer->prev = m_tail;
		timer->next = m_tail->next;
		m_tail->next = timer;
		m_tail = timer;
		m_size++;
		return true;
	}

	return false;
}

/*
*
* 心跳滴答函数
*
*/
void TimerSortList::tick()
{
	/*链表为空*/
	if (m_size == 0 || !m_head)
		return;

	time_t cur_time = time(NULL);      //获取当前时间
	TimerUnit* tmp = m_head;

	while (tmp)
	{
		/*定时时间还没到期*/
		if (cur_time < tmp->expire)
			break;
		/*定时时间到期*/
		else
		{
			/*执行回调函数*/
			tmp->cb_func(tmp->user_data);

			/*删除到期定时器*/
			del_timer(tmp);

			tmp = tmp->next;
		}
	}
}



