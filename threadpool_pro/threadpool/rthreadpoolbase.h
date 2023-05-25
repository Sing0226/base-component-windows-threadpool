#include "threadpool/rthreadpoolconst.h"
#include <Windows.h>

#ifndef _THREAD_POOL_BASE_H__
#define _THREAD_POOL_BASE_H__

#define TP_MAX_THREAD_DEADLINE_CHECK   5000

typedef enum tagRThreadWaitType
{
	tpStop, 
	tpContinue,
	tpTimeOut,
	tpError,
}RThreadWaitType;


class RLockObject
{
public:
	virtual BOOL Lock(DWORD dwTimeout = INFINITE) = 0;
	virtual BOOL UnLock() = 0;
};

class RCriticalSection : public RLockObject
{
	// 屏蔽赋值构造和拷贝构造
	DISABLE_COPY_AND_ASSIGNMENT(RCriticalSection);
public:
	RCriticalSection();
	virtual ~RCriticalSection();

	BOOL Lock(DWORD dwTimeout = INFINITE);
	BOOL UnLock();
	BOOL TryLock();
#ifdef _DEBUG
	BOOL IsLocked() const;
#endif
private:
	CRITICAL_SECTION m_CritSec;
#ifdef _DEBUG
	DWORD   m_currentOwner;     //当前线程ID
	DWORD   m_lockCount;        //用于跟踪线程进入关键代码段的次数,可以用于调试
#endif
};


#endif