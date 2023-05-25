#include "threadpool/rbasejob.h"
#include "threadpool/rthreadpoolconst.h"
#include "threadpool/rthreadpool.h"
#include "rslogger_declare.h"
#include "rslog.h"
#include "rslogging.h"


///////////////////////////////////////////// RBaseJob ///////////////////////////////////////////////////
//  初始化
RBaseJob::RBaseJob(int job_priority):
job_priority_(job_priority)
{
	job_index_					= 0;
	thread_pool_				= NULL;
	stop_job_event_				= NULL;
	sync_cancel_job_event_		= NULL;

	sync_cancel_job_event_ = CreateEvent(NULL,TRUE,FALSE,NULL);
	if(sync_cancel_job_event_ == INVALID_HANDLE_VALUE)
	{
		RSLOG_DEBUG <<"create cancel_job_envent failed!";
		sync_cancel_job_event_ = NULL;
	}
}

RBaseJob::~RBaseJob()
{
	TPASSERT(NULL == stop_job_event_);

	CloseHandle(sync_cancel_job_event_);
	sync_cancel_job_event_ = NULL;
}

bool RBaseJob::operator< (const RBaseJob& other) const
{
	COMPARE_MEM_LESS(job_priority_, other);
	COMPARE_MEM_LESS(job_index_, other);

	return true;
}

int RBaseJob::getJobIndex() const
{
	return job_index_;
}


BOOL RBaseJob::requestCancel()
{
	BOOL bRet = FALSE;
	API_VERIFY(SetEvent(stop_job_event_));
	return bRet;
}

void RBaseJob::_notifyProgress(LONG64 nCurPos, LONG64 nTotalSize)
{
	TPASSERT(thread_pool_);
	thread_pool_->_notifyJobProgress(this, nCurPos, nTotalSize);
}

void RBaseJob::_notifyCancel()
{
	thread_pool_->_notifyJobCancel(this);
}

void RBaseJob::_notifyError(DWORD dwError, LPCTSTR pszDescription)
{
	thread_pool_->_notifyJobError(this, dwError, pszDescription);
}

VOID RBaseJob::_finalize()
{

}

void RBaseJob::_notifyIndex()
{
}

BOOL RBaseJob::initialize(/*CBasePayService* pBasePayService, void* param*/)
{
	TPASSERT(NULL == stop_job_event_);
	BOOL bRet = TRUE;

	return bRet;
}

RThreadWaitType RBaseJob::_getJobWaitType(DWORD dwMilliseconds /* = INFINITE*/) const
{
	RThreadWaitType waitType = tpError;
	HANDLE waitEvent[] = 
	{
		stop_job_event_,
		thread_pool_->stop_event_,
		thread_pool_->continue_event_
	};
	DWORD dwResult = ::WaitForMultipleObjects(_countof(waitEvent), waitEvent, FALSE, dwMilliseconds);
	switch (dwResult)
	{
	case WAIT_OBJECT_0:
		waitType = tpStop;		// Job Stop Event
		break;
	case WAIT_OBJECT_0 + 1:
		waitType = tpStop;		// Thread Pool Stop Event
		break;
	case WAIT_OBJECT_0 + 2:
		waitType = tpContinue;	// Thread Pool Continue Event
		break;
	case WAIT_TIMEOUT:
		waitType = tpTimeOut;
		break;
	default:
		TPASSERT(FALSE);
		waitType = tpError;
		break;
	}
	return waitType;
}

// 同步取消

HANDLE RBaseJob::get_cancel_job_event()
{
	return sync_cancel_job_event_;
}

void RBaseJob::signal_sync_cancel_job_event()
{
	RSLOG_DEBUG<<"signal_sync_cancel_job_event entry...";
	if(sync_cancel_job_event_ != NULL)
	{
		SetEvent(sync_cancel_job_event_);
	}
	RSLOG_DEBUG<<"signal_sync_cancel_job_event leave";
}