/************************************************
* 				event
* 
* desc: epoll事件中心接口声明
* author: kwanson
* email: CSDN kwanson
*************************************************/

#ifndef __EVENT_H__
#define __EVENT_H__

#include <sys/epoll.h>

#include <map>

#include "mutex.h"
#include "queue.h"
#include "ISocket.h"
#include "threadpool.h"
//套接字对象将自身注册到框架中，框架即可利用epoll对其套接字进行事件监测；当事件产生时通知相应的套接字对象。
namespace Event
{
enum EventConfig
{
	NEVENT_MAX = 1024,	  //事件中心最大注册套接字数
};
enum EventType
{
	EIN = EPOLLIN,		  // 读事件
	EOUT = EPOLLOUT,	  // 写事件
	ECLOSE = EPOLLRDHUP,  // 对端关闭连接或者写半部
	EPRI = EPOLLPRI,	  // 紧急数据到达
	EERR = EPOLLERR,	  // 错误事件
	EET = EPOLLET, 		  // 边缘触发
	EDEFULT = EIN | ECLOSE | EERR | EET
};

class INetObserver
{
	friend class CNetObserver;
	public:
		virtual ~INetObserver(){};
		
	protected:
		// desc: 读事件回调函数
		// param: /套接字描述符
		// return: void
		virtual void handle_in(int) = 0;

		// desc: 写事件回调函数
		// param: /套接字描述符
		// return: void		
		virtual void handle_out(int) = 0;

		// desc: 关闭事件回调函数
		// param: /套接字描述符
		// return: void
		virtual void handle_close(int) = 0;

		// desc: 错误事件回调函数
		// param: /套接字描述符
		// return: void		
		virtual void handle_error(int) = 0;
};

// 套接字继承于IEventHandle, 注册进入事件中心(Event), 从而获得事件通知
// 所有需要注册进入事件中心的套接字对象只需继承此基类即可
// 堆上的对象只能在handle_close中释放自己
// 事件处理程序
// 事件处理程序提供了一组接口，每个接口对应了一种类型的事件，供Reactor在相应的事件发生时调用，执行相应的事件处理。通常它会绑定一个有效的句柄。
// 对应到libevent中，就是event结构体。
// 注册与关闭事件实际是通过CEventProxy调用IEvent接口
// 相当于epoll中的一个事件
class IEventHandle: public INetObserver
{
	public:
		// desc: 注册进入事件中心(Event)
		// param: fd/套接字描述符 type/事件类型
		// return: 0/成功 -1/失败	
		int register_event(int fd, EventType type = EDEFULT);

		// desc: 注册进入事件中心(Event)
		// param: socket/套接字对象 type/事件类型
		// return: 0/成功 -1/失败	
		int register_event(Socket::ISocket &socket, EventType type = EDEFULT);

		// desc: 关闭事件
		// param: fd/套接字描述符
		// return: 0/成功 -1/失败	
		int shutdown_event(int fd);

		// desc: 关闭事件
		// param: socket/套接字对象
		// return: 0/成功 -1/失败	
		int shutdown_event(Socket::ISocket &);
};
// 事件处理程序
// 事件处理程序提供了一组接口，每个接口对应了一种类型的事件，供Reactor在相应的事件发生时调用，执行相应的事件处理。通常它会绑定一个有效的句柄。
// 对应到libevent中，就是event结构体。
// 注册与关闭事件实际是通过CEventProxy调用IEvent接口
// 回调函数通过INetObserver &m_obj的方法调用
class CNetObserver: public INetObserver
{
	friend class CEvent;
	public:
		CNetObserver(INetObserver &, EventType);
		~CNetObserver();
		inline void addref();
		inline void subref();
		inline bool subref_and_test();
		inline void selfrelease();
		inline EventType get_regevent();
		inline const INetObserver *get_handle();
		
	protected:
		void handle_in(int);
		void handle_out(int);
		void handle_close(int);
		void handle_error(int);
		
	private:
		//事件epoll参数
		EventType m_regevent;
		// ?
		INetObserver &m_obj;

		int32_t m_refcount;
		Pthread::CMutex m_refcount_mutex;
};

//Reactor,反应器,是事件管理的接口，内部使用event demultiplexer注册、注销事件；并运行事件循环，当有事件进入“就绪”状态时，调用注册事件的回调函数处理事件。
//对应到libevent中，就是event_base结构体。
class IEvent
{		
	public:
		virtual ~IEvent(){};

		// desc: 注册进入事件中心
		// param: fd/套接字描述符 type/事件类型
		// return: 0/成功 -1/失败	
		virtual int register_event(int, IEventHandle *, EventType) = 0;

		// desc: 关闭事件
		// param: fd/套接字描述符
		// return: 0/成功 -1/失败	
		virtual int shutdown_event(int) = 0;
};
// 事件注册进m_epollfd,收到事件后向线程池增加任务以处理事件
// 事件继承自IEvent，线程池执行IThreadHandle中的threadhandle()
class CEvent: public IEvent, public ThreadPool::IThreadHandle
{
	public:
		CEvent(size_t neventmax);
		~CEvent();
		int register_event(int, IEventHandle *, EventType);
		int shutdown_event(int);
		
	protected:
		// 事件中心创建一条线程eventwait_thread，在这里调用epoll_wait等待事件。
		// 事件发生后将事件放入队列并向线程池投入任务。
		// 线程池执行threadhandle接口
		void threadhandle();
		
	private:
		enum ExistRet{
			NotExist, HandleModify, TypeModify, Modify, Existed,
		};

		enum Limit{
			EventBuffLen = 1024, CommitAgainNum = 2,
		};

		typedef std::map<int, CNetObserver *> EventMap_t;
		typedef std::map<int, EventType> EventTask_t;
		
	private:
		ExistRet isexist(int fd, EventType type, IEventHandle *handle);
		//  记录注册套接字对象进入map m_eventreg.
		int record(int fd, EventType eventtype, IEventHandle *handle);
		int detach(int fd, bool release = false);
		CNetObserver *get_observer(int fd);
		int pushtask(int fd, EventType event);
		int poptask(int &fd, EventType &event);
		size_t tasksize();
		int cleartask(int fd);
		int unregister_event(int);
		// 事件中心线程
		// 事件中心创建一条线程eventwait_thread，在这里调用epoll_wait等待事件。
		// 事件发生后将事件放入队列并向线程池投入任务。
		// 线程池执行threadhandle接口
		// 即每个Event除了线程池外还有一个线程用于epoll_wait
		static void *eventwait_thread(void *arg);
		
	private:
		int m_epollfd;
		//记录事件,下标是事件对应的fd
		EventMap_t m_eventreg;
		Pthread::CMutex m_eventreg_mutex;
		EventTask_t m_events;
		Pthread::CMutex m_events_mutex;
		
		struct epoll_event m_eventbuff[EventBuffLen];
		
		pthread_t m_detectionthread;
		// 指向本线程的线程池
		ThreadPool::IThreadPool *m_ithreadpool;
};

//IEvent单例
class CEventProxy: public IEvent
{
	public:
		static CEventProxy *instance(); 
		int register_event(int, IEventHandle *,EventType);
		int register_event(Socket::ISocket &, IEventHandle *,EventType);
		int shutdown_event(int);
		int shutdown_event(Socket::ISocket &);
		
	private:
		CEventProxy(size_t neventmax);
		~CEventProxy();
		
	private:
		IEvent *m_event;
};

}
#endif
