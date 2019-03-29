/************************************************
* 				threadpool
* 
* desc: 线程池
* author: kwanson
* email: CSDN kwanson
*************************************************/

#include "threadpool.h"
#include "log.h"
#include "utils.h"

namespace ThreadPool
{
CThreadPool::CThreadPool(size_t threadnum, size_t tasknum): 
	m_taskqueue(tasknum), m_hasleader(false)
{
	m_threadnum = (threadnum > THREADNUM_MAX)? THREADNUM_MAX: threadnum;
	create_threadpool();
}
CThreadPool::~CThreadPool() 
{
	destroy_threadpool();
}

int CThreadPool::pushtask(IThreadHandle *handle, bool block)
{
	if(block)
		return m_taskqueue.push(handle);
	return m_taskqueue.push_nonblock(handle);
}
//选出一个线程作为Leader
void CThreadPool::promote_leader()
{
	Pthread::CGuard guard(m_identify_mutex);
	while(m_hasleader){	// more than one thread can return 条件变量存在多次返回的可能
		m_befollower_cond.wait(m_identify_mutex);//等待领导者成为追随者
	}
	m_hasleader = true;//成为Leader后该线程设置一下标志m_hasleader
}

//从领导者成为追随者,通知其他线程可以成为领导者
void CThreadPool::join_follwer()
{
	Pthread::CGuard guard(m_identify_mutex);
	m_hasleader = false;
	m_befollower_cond.signal();
}


void CThreadPool::create_threadpool()
{
	pthread_attr_t thread_attr;
	pthread_attr_init(&thread_attr);
	for(size_t i = 0; i < m_threadnum; i++){
		pthread_t tid = 0;
		if(pthread_create(&tid, &thread_attr, process_task, (void *)this) < 0){
			errsys("create thread[%d] filed\n", (int)i);
			continue;
		}

		m_thread.push_back(tid);
	}
	pthread_attr_destroy(&thread_attr);//销毁一个目标结构，并且使它在重新初始化之前不能重新使用
	trace("create thread pool, thread number %d\n", (int)m_thread.size());
}


void CThreadPool::destroy_threadpool()
{
	void *retval = NULL;
	vector_tid_t::iterator itor = m_thread.begin();
	for(; itor != m_thread.end(); itor++){
		if(pthread_cancel(*itor) < 0 || pthread_join(*itor, &retval) < 0){
			errsys("destroy thread[%d]\n", (int)(*itor));
			continue;
		}
	}

	m_thread.clear();
	trace("destroy thread pool... done\n");
}

//处理该任务，也就是回调对象的threadhandle函数
void *CThreadPool::process_task(void * arg)
{
	CThreadPool &threadpool = *(CThreadPool *)arg;
	while(true){
		//等待成为领导者
		threadpool.promote_leader();	
		IThreadHandle *threadhandle = NULL;
		//成为领导者后取出任务
		int ret = threadpool.m_taskqueue.pop(threadhandle);
		//成为追随者 取消m_hasleader标志
		threadpool.join_follwer();		
		//执行任务
		if(ret == 0 && threadhandle)
			threadhandle->threadhandle();
	}	

	pthread_exit(NULL);
}

}
