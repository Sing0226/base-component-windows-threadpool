#include "threadpool/rthreadpoolbase.h"
#include "rslogger_declare.h"
#include "rslog.h"
#include "rslogging.h"


RCriticalSection::RCriticalSection()
{
#if (_WIN32_WINNT >= 0x0403)
	//使用 InitializeCriticalSectionAndSpinCount 可以提高性能
	BOOL bInit = ::InitializeCriticalSectionAndSpinCount(&m_CritSec, 0x4000);
	if (bInit == FALSE)
	{
		RSLOG_ERROR << "initialize cs failed!";
	}
#else
	::InitializeCriticalSection(&m_CritSec);
#endif

#ifdef _DEBUG
	m_lockCount = 0;
	m_currentOwner = 0;
#endif
}

RCriticalSection::~RCriticalSection()
{
#ifdef _DEBUG
	TPASSERT(m_lockCount == 0); // 析构前必须完全释放，否则可能造成死锁
#endif
	::DeleteCriticalSection(&m_CritSec);
}

BOOL RCriticalSection::Lock(DWORD dwTimeout/* = INFINITE*/)
{
	UNREFERENCED_PARAMETER( dwTimeout );	
#ifdef _DEBUG
	char log_tmp[100];
	DWORD us = GetCurrentThreadId();
	DWORD currentOwner = m_currentOwner;
	if (currentOwner && (currentOwner != us)) // already owned, but not by us
	{
		sprintf_s(log_tmp,"Thread %d begin to wait for CriticalSection %p Owned by %d",
			us, &m_CritSec, currentOwner);
		RSLOG_DEBUG << log_tmp;
	}
#endif
	::EnterCriticalSection(&m_CritSec);

#ifdef _DEBUG
	if (0 == m_lockCount++) // we now own it for the first time.  Set owner information
	{
		m_currentOwner = us;
		sprintf_s(log_tmp,"Thread %d now owns CriticalSection %p", m_currentOwner, &m_CritSec);
		RSLOG_DEBUG  << log_tmp;
	}
#endif
	return TRUE;
}

BOOL RCriticalSection::UnLock()
{
#ifdef _DEBUG
	DWORD us = GetCurrentThreadId();
	TPASSERT( us == m_currentOwner ); //just the owner can unlock it
	TPASSERT( m_lockCount > 0 );
	if ( 0 == --m_lockCount ) 
	{
		// begin to unlock
		char log_tmp[100];
		sprintf_s(log_tmp,"Thread %d releasing CriticalSection %p", m_currentOwner, &m_CritSec);
		RSLOG_DEBUG  << log_tmp;
		m_currentOwner = 0;
	}
#endif
	::LeaveCriticalSection(&m_CritSec);
	return TRUE;
}

BOOL RCriticalSection::TryLock()
{
	BOOL bRet = TryEnterCriticalSection(&m_CritSec);
	return bRet;
}

#ifdef _DEBUG
BOOL RCriticalSection::IsLocked() const
{
	return (m_lockCount > 0);
}
#endif