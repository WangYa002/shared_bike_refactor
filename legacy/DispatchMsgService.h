/*
负责分发消息服务模块，其实就是把外部收到的消息，转化成内部事件，也就是data->msg->event的解码过程
然后再把事件投递至线程池的消息队列，由线程池调用其process方法对事件进行处理，最终调用每个event的handle方法
来处理event,此时每个event handler需要subscribe该event后才会被调用到
*/
#ifndef BRK_SERVICE_DISPATCH_EVENT_SERVICE_H_
#define BRK_SERVICE_DISPATCH_EVENT_SERVICE_H_

#include <map>
#include <vector>
#include <queue>
#include "ievent.h"
#include "eventtype.h"
#include "iEeventHandler.h"
#include "thread_pool.h"
#include "NetworkInterface.h"
#include "logger.h"

class DispatchMsgService
{

protected:
	DispatchMsgService();

public:
	
	virtual ~DispatchMsgService();

	virtual BOOL open();
	virtual void close();

	virtual void subscribe(u32 eid, iEventHandler* handler);
	virtual void unsubscribe(u32 eid, iEventHandler* handler);
	//把事件投递到线程池进行处理
	virtual i32 enqueue(iEevent* ev);
	//对具体的事件进行分发处理
	virtual iEevent* process(const iEevent* ev);       //对具体的事件进行分发处理
	//线程池回调函数,对于C++成员函数,会返回this,即svc(this,void* argv)
	static void svc(void* argv);
	

	static DispatchMsgService* getInstance();

	static iEevent* parseEvent(const char* message , u32 len, u32 eid);

	void handleAllResponseEvent(NetworkInterface* interface);

protected:
	thread_pool_t* tp;

	static DispatchMsgService* DMS_;

	typedef std::vector<iEventHandler*> T_EventHandlers;
	typedef std::map<u32, T_EventHandlers> T_EventHandlersMap;
	T_EventHandlersMap subscribers_;
	bool svr_exit_;

	static std::queue<iEevent*> response_events; //处理完请求的响应事件
	static pthread_mutex_t queue_mutex; //互斥锁

private:
	void AssembleHB(ConnectSession* cs, iEevent* ev, i32 eid);


};


#endif // !BRK_SERVICE_DISPATCH_EVENT_SERVICE_H_











