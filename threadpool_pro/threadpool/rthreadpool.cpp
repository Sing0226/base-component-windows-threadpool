#include "threadpool/rthreadpool.h"
#include <process.h>							//for _beginthreadex
#include "rslogger_declare.h"
#include "rslog.h"
#include "rslogging.h"



RThreadPool::RThreadPool (RThreadPoolCallBack* pCallBack)
	: m_pCallBack(pCallBack)
	, min_number_threads_(0)
	, max_number_threads_(1)
	, job_index_(0)
	, running_job_number_(0)
	, cur_number_threads_(0)
	, running_thread_number_(0)
	, job_thread_handles_(NULL)
	, job_thread_ids_(NULL)
{
	stop_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	TPASSERT(NULL != stop_event_);

	all_thread_complete_event_ = ::CreateEvent(NULL, TRUE, TRUE, NULL);
	TPASSERT(NULL != all_thread_complete_event_);

	continue_event_ = ::CreateEvent(NULL, TRUE, TRUE, NULL);
	TPASSERT(NULL != continue_event_);

	//将可以同时做的工作数设置为 MAXLONG -- 目前暂时不考虑队列中的个数
	job_needed_to_do_semaphore_ = ::CreateSemaphore(NULL, 0, MAXLONG, NULL);
	TPASSERT(NULL != job_needed_to_do_semaphore_);

	//创建调整线程个数的信号量
	subtract_thread_semaphore_ = CreateSemaphore(NULL, 0, MAXLONG, NULL);
	TPASSERT(NULL != subtract_thread_semaphore_);

	// 预提交job
	prepare_submit_job_mtx_ = CreateMutex(NULL,FALSE,NULL);
	TPASSERT(NULL != prepare_submit_job_mtx_);

	prepare_submit_job_sempahore_ = CreateSemaphore(NULL,0,MAXLONG,NULL);
	TPASSERT(NULL != prepare_submit_job_sempahore_);

	prepare_submit_job_handle_arr[0] = prepare_submit_job_mtx_;
	prepare_submit_job_handle_arr[1] = prepare_submit_job_sempahore_;

	thread_pool_is_running_				= FALSE;
	prepare_submit_job_thread_handle_	= NULL;
	prepare_submit_job_thread_id_		= 0;

	clear_job_mtx_				= CreateMutex(NULL,FALSE,NULL);
	TPASSERT(NULL != clear_job_mtx_);

	clear_job_sempahore_		= CreateSemaphore(NULL,0,MAXLONG,NULL);
	TPASSERT(NULL != clear_job_sempahore_);

	clear_job_handle_arr[0]		= clear_job_mtx_;
	clear_job_handle_arr[1]		= clear_job_sempahore_;

	thread_pool_submit_flag_	= FALSE;

	// 启动清理线程
	clear_job_thread_handle_ = (HANDLE) _beginthreadex( NULL, 0, _clearJobThreadProc, this, 0, &clear_job_thread_id_);
}


RThreadPool::~RThreadPool()
{
	BOOL bRet = FALSE;
	RSLOG_DEBUG<<"Stop all threads,wait all exit";
	API_VERIFY(stop(INFINITE));

	// 释放预提交业务
	_clearPrepareSubmtJobs();
	TPASSERT(NULL == prepare_submit_job_thread_handle_);	
	TPASSERT(0 == prepare_submit_job_thread_id_);	

	RSLOG_DEBUG<<"Destroy pool";
	_destroyPool();

	TPASSERT(set_waiting_jobs_.empty());
	TPASSERT(map_doing_hobs_.empty());
	TPASSERT(0 == running_thread_number_);				//析构时所有的线程都要结束


	TPASSERT(FALSE == thread_pool_is_running_);	

	// 提交清理线程job
	_submitExitClearThreadJob();
	RSLOG_DEBUG<<"Exit clear job thread";
	WaitForSingleObject(clear_job_thread_handle_,INFINITE);
	SAFE_CLOSE_HANDLE(clear_job_thread_handle_,NULL);
	clear_job_thread_id_ = 0;
	
}


BOOL RThreadPool::start(LONG min_number_threads, LONG max_number_threads)
{
	RSLOG_DEBUG<<"TPThreadPool::start entry..";
	BOOL bRet = TRUE;

	if(thread_pool_is_running_ == TRUE)
	{
		RSLOG_DEBUG<<"Thread pool is start,no need start again!";
	}

	CFAutoLock<RLockObject> locker(&thread_pool_start_work_lock_);
	// 启动工作预提交线程
	prepare_submit_job_thread_handle_ = (HANDLE) _beginthreadex( NULL, 0, _prepareSubmitJobThreadProc, this, 0, &prepare_submit_job_thread_id_);
	thread_pool_submit_flag_ = TRUE;

	RSLOG_DEBUG<<"TPThreadPool::start, ThreadNum is ["<<min_number_threads<<"-"<<max_number_threads<<"]";

	TPASSERT( 0 <= min_number_threads );
	TPASSERT( min_number_threads <= max_number_threads );       

	min_number_threads_ = min_number_threads;
	max_number_threads_ = max_number_threads;

	API_VERIFY(ResetEvent(stop_event_));
	API_VERIFY(ResetEvent(all_thread_complete_event_));
	API_VERIFY(SetEvent(continue_event_));										// 设置继续事件，保证各个工作线程能运行

	{
		CFAutoLock<RLockObject>   locker(&threads_lock_);						// 加锁，addThread不加锁
		TPASSERT(NULL == job_thread_handles_);
		if(NULL == job_thread_handles_)											//防止多次调用Start
		{
			job_thread_handles_ = new HANDLE[max_number_threads_];					//分配max_number_threads个线程的空间
			ZeroMemory(job_thread_handles_,sizeof(HANDLE) * max_number_threads_);

			job_thread_ids_ = new DWORD[max_number_threads_];
			ZeroMemory(job_thread_ids_,sizeof(DWORD) * max_number_threads_);

			_addJobThread(min_number_threads_);										//开始时只创建 min_number_threads_ 个线程
			TPASSERT(cur_number_threads_ == min_number_threads_);
		}
	}

	thread_pool_is_running_ = TRUE;
	RSLOG_DEBUG<<"TPThreadPool::start leave..";
	return bRet;
}


BOOL RThreadPool::_addJobThread(int thread_num)
{
	RSLOG_DEBUG<<"TPThreadPool::_addJobThread entry..";

	BOOL bRet = TRUE;
	{
		// CFAutoLock<RLockObject> locker(&threads_lock_); // 调用该函数之前都有大锁进行保护
		if (cur_number_threads_ + thread_num > max_number_threads_)
		{
			TPASSERT(FALSE);
			//超过最大个数，不能再加了
			SetLastError(ERROR_INVALID_PARAMETER);
			RSLOG_DEBUG<<"增加的线程数超过了最大线程数...";
			bRet = FALSE;
		}
		else
		{
			unsigned int threadId = 0;
			for(int i = 0;i < thread_num; i++)
			{
				TPASSERT(NULL == job_thread_handles_[cur_number_threads_]);
				job_thread_handles_[cur_number_threads_] = (HANDLE) _beginthreadex( NULL, 0, _jobThreadProc, this, 0, &threadId);
				TPASSERT(NULL != job_thread_handles_[cur_number_threads_]);

				job_thread_ids_[cur_number_threads_] = threadId;
				cur_number_threads_++;

				RSLOG_DEBUG<<"TPThreadPool::_addJobThread, ThreadId="<<threadId<<",CurNumThreads="<<cur_number_threads_;
			}
			bRet = TRUE;
		}
	}

	RSLOG_DEBUG<<"TPThreadPool::_addJobThread leave..";
	return bRet;
}


void RThreadPool::_submitPrepareExitThreadJob()
{
	RSLOG_DEBUG<<"TPThreadPool:: _submitPrepareExitThreadJob entry...";
	TPASSERT(NULL != stop_event_);									//  如果调用 _DestroyPool后，就不能再次调用该函数

	BOOL bRet = FALSE;

	DWORD dw = WaitForSingleObject(prepare_submit_job_mtx_,INFINITE);
	if(dw == WAIT_OBJECT_0)
	{
		PreSubmitJob preSubmitJob;
		preSubmitJob.pJob = NULL;
		int *pExitPrepareSubmitJoxIndex = new int;
		*pExitPrepareSubmitJoxIndex = -256;
		preSubmitJob.pOutJobIndex = pExitPrepareSubmitJoxIndex;

		LONG lPrevCount;
		BOOL fOK = ReleaseSemaphore(prepare_submit_job_sempahore_,1L,&lPrevCount);
		if(fOK)
		{
			// 加入消息队列中
			char log_tmp[100];
			sprintf_s(log_tmp,"预提交退出预提交线程Job,lPrevCount = %d,Job = %p",lPrevCount,preSubmitJob.pJob);
			RSLOG_DEBUG<<log_tmp;

			// deque_prepare_submit_job_.push_front(preSubmitJob);
			deque_prepare_submit_job_.push_back(preSubmitJob);
		}
		else
		{
			RSLOG_DEBUG<<"没有预提交Job";
		}
		ReleaseMutex(prepare_submit_job_mtx_);
	}

	RSLOG_DEBUG<<"TPThreadPool::_submitPrepareExitThreadJob leave...";
}

BOOL RThreadPool::stop(DWORD dwTimeOut)
{	
	CFAutoLock<RLockObject> locker(&thread_pool_start_work_lock_);
	RSLOG_DEBUG<<"TPThreadPool::stop entry...";
	if(thread_pool_is_running_ == FALSE)
	{
		RSLOG_DEBUG<<"TPThreadPool had stop";
	}
	_submitPrepareExitThreadJob();

	BOOL bRet = TRUE;
	API_VERIFY(SetEvent(stop_event_));

	RSLOG_DEBUG<<"Wait Prepare Submit Job thread exit";
	WaitForSingleObject(prepare_submit_job_thread_handle_,INFINITE);
	SAFE_CLOSE_HANDLE(prepare_submit_job_thread_handle_,NULL);
	prepare_submit_job_thread_id_ = 0;

	_wait(dwTimeOut);

	thread_pool_is_running_ = FALSE;

	RSLOG_DEBUG<<"TPThreadPool::stop leave...";
	return bRet;
}

BOOL RThreadPool::_wait(DWORD dwTimeOut /* = FTL_MAX_THREAD_DEADLINE_CHECK */)
{
	RSLOG_DEBUG << "TPThreadPool::wait entry...";
	RSLOG_DEBUG << "dwTimeOut=" << dwTimeOut;

	BOOL bRet = TRUE;
	DWORD dwResult = WaitForSingleObject(all_thread_complete_event_, dwTimeOut);
	switch (dwResult)
	{
	case WAIT_OBJECT_0:							//所有的线程都结束了
		bRet = TRUE;
		break;
	case WAIT_TIMEOUT:
		RSLOG_DEBUG << "Not all thread over in " << dwTimeOut << "millisec\n";
		TPASSERT(FALSE);
		SetLastError(ERROR_TIMEOUT);
		bRet = FALSE;
		break;
	default:
		TPASSERT(FALSE);
		bRet = FALSE;
		break;
	}

	{
		CFAutoLock<RLockObject> locker(&threads_lock_);
		for (LONG i = 0; i < cur_number_threads_; i++)
		{
			SAFE_CLOSE_HANDLE(job_thread_handles_[i],NULL);
		}
		SAFE_DELETE_ARRAY(job_thread_handles_);
		SAFE_DELETE_ARRAY(job_thread_ids_);
		cur_number_threads_ = 0;
	}

	RSLOG_DEBUG << "TPThreadPool::wait leave,bRet = " << bRet;
	return bRet;
}

//BOOL TPThreadPool::_stopAndWait(DWORD dwTimeOut /* = TP_MAX_THREAD_DEADLINE_CHECK */)
//{
//	RSLOG_DEBUG<<"TPThreadPool::stopAndWait entry...";
//	
//	BOOL bRet = TRUE;
//	API_VERIFY(stop());
//	API_VERIFY(_wait(dwTimeOut));
//	RSLOG_DEBUG<<"TPThreadPool::stopAndWait leave...";
//	return bRet;
//}


BOOL RThreadPool::pause()
{
	RSLOG_DEBUG<<"TPThreadPool::pause entry...";

	BOOL bRet = FALSE;

	API_VERIFY(::ResetEvent(continue_event_));

	RSLOG_DEBUG<<"TPThreadPool::pause leave...";

	return bRet;
}

BOOL RThreadPool::resume()
{
	RSLOG_DEBUG<<"TPThreadPool::resume entry...";

	BOOL bRet = FALSE;

	API_VERIFY(::SetEvent(continue_event_));

	RSLOG_DEBUG<<"TPThreadPool::resume leave...";

	return bRet;
}

BOOL RThreadPool::_clearUndoWork()
{
	RSLOG_DEBUG<<"TPThreadPool::_clearUndoWork entry...";

	BOOL bRet = TRUE;
	{
		CFAutoLock<RLockObject> locker(&waiting_jobs_lock_);
		RSLOG_DEBUG<<" waitingJob Number is"<< set_waiting_jobs_.size();
		while (!set_waiting_jobs_.empty())
		{
			//释放对应的信标对象，其个数和 m_WaitingJobs 的个数是一致的
			DWORD dwResult = WaitForSingleObject(job_needed_to_do_semaphore_, TP_MAX_THREAD_DEADLINE_CHECK); 
			API_VERIFY(dwResult == WAIT_OBJECT_0);

			WaitingJobContainer::iterator iterBegin = set_waiting_jobs_.begin();
			RBaseJob* pJob = *iterBegin;
			TPASSERT(pJob);
			_notifyJobCancel(pJob);
			pJob->_onCancelJob();
			_addClearJobDeque(pJob->getJobIndex(),pJob);
			set_waiting_jobs_.erase(iterBegin);
		}
	}

	RSLOG_DEBUG<<"TPThreadPool::_clearUndoWork leave...";

	return bRet;
}

// 清除当前预提交工作，
BOOL RThreadPool::_clearPrepareSubmtJobs()
{
	RSLOG_DEBUG<<"TPThreadPool::_clearPrepareSubmtJobs entry...";
	BOOL bRet = TRUE;

	// CFAutoLock<RLockObject> locker(&prepare_submit_job_lock_);
	RSLOG_DEBUG<<" prepare submit job number is"<< deque_prepare_submit_job_.size();
	DWORD dw = WaitForSingleObject(prepare_submit_job_mtx_,INFINITE);
	if(dw == WAIT_OBJECT_0)
	{
		while (!deque_prepare_submit_job_.empty())
		{
			//释放对应的信标对象，其个数和 prepare submit jobs 的个数是一致的
			DWORD dwResult = WaitForSingleObject(prepare_submit_job_sempahore_, TP_MAX_THREAD_DEADLINE_CHECK); 
			API_VERIFY(dwResult == WAIT_OBJECT_0);

			Deque_Prepare_Submit_Job::iterator iterPreSubJob = deque_prepare_submit_job_.begin();
			RBaseJob* pJob = (*iterPreSubJob).pJob;
			TPASSERT(pJob);
			_notifyJobCancel(pJob);
			pJob->_onCancelJob();					// 释放JOB
			// 添加到清理队列
			_addClearJobDeque(*(iterPreSubJob->pOutJobIndex),iterPreSubJob->pJob);
			deque_prepare_submit_job_.erase(iterPreSubJob);
		}
	}
	ReleaseMutex(prepare_submit_job_mtx_);
	
	SAFE_CLOSE_HANDLE(prepare_submit_job_mtx_,NULL);
	SAFE_CLOSE_HANDLE(prepare_submit_job_sempahore_,NULL);
	TPASSERT(deque_prepare_submit_job_.empty());

	RSLOG_DEBUG << "TPThreadPool::_clearPrepareSubmtJobs leave...";

	return bRet;
}

//////////////////////////////////////////////////////////////////////////
// 预提交该工作线程执行函数
unsigned int RThreadPool::_prepareSubmitJobThreadProc(void* pParam)
{
	RSLOG_DEBUG << "TPThreadPool::_prepareSubmitJobThreadProc entry...";

	RThreadPool* pThreadPool = (RThreadPool*) pParam;
	while(TRUE)
	{
		BOOL bOK = WaitForMultipleObjects(_countof(pThreadPool->prepare_submit_job_handle_arr),
			pThreadPool->prepare_submit_job_handle_arr, TRUE, INFINITE) == WAIT_OBJECT_0;
		if(bOK)
		{
			PreSubmitJob preSubJob;
			preSubJob = pThreadPool->deque_prepare_submit_job_.front();
			pThreadPool->deque_prepare_submit_job_.pop_front();
			ReleaseMutex(pThreadPool->prepare_submit_job_mtx_);

			TPASSERT(NULL != pThreadPool->stop_event_);									//  如果调用 _DestroyPool后，就不能再次调用该函数

			RSLOG_DEBUG << "TPThreadPool::_prepareSubmitJobThreadProc start submit wait job";

			BOOL bRet = FALSE;
			if(preSubJob.pJob == NULL && preSubJob.pOutJobIndex !=NULL && *preSubJob.pOutJobIndex == -256)
			{
				delete preSubJob.pOutJobIndex;
				preSubJob.pOutJobIndex = NULL;
				goto final_end;
			}

			//加入Job并且唤醒一个等待线程
			{
				CFAutoLock<RLockObject> locker(&(pThreadPool->waiting_jobs_lock_));

				if (preSubJob.pOutJobIndex)
				{
					*(preSubJob.pOutJobIndex) = pThreadPool->job_index_;
				}

				pThreadPool->set_waiting_jobs_.insert(preSubJob.pJob);
				API_VERIFY(ReleaseSemaphore(pThreadPool->job_needed_to_do_semaphore_, 1L, NULL));
			}

			SwitchToThread();//唤醒等待的线程，使得其他线程可以获取Job -- 注意 CFAutoLock 的范围

			{
				//	当所有的线程都在运行Job时，则需要增加线程  -- 不对 m_nRunningJobNumber 加保护(只是读取),读取的话也要加保护
				CFAutoLock<RLockObject> locker(&pThreadPool->threads_lock_);
				TPASSERT(pThreadPool->running_job_number_ <= pThreadPool->cur_number_threads_);
				BOOL bNeedMoreThread = (pThreadPool->running_job_number_ == pThreadPool->cur_number_threads_) && 
					(pThreadPool->cur_number_threads_ < pThreadPool->max_number_threads_); 
				if (bNeedMoreThread)
				{
					API_VERIFY(pThreadPool->_addJobThread(1L));				// 每次增加一个线程
				}

				char log_tmp[250];
				sprintf_s(log_tmp,
					"CFThreadPool::SubmitJob, pJob[%d] = %p, running_job_number_=%d, cur_number_threads_=%d, bNeedMoreThread=%d \n",
					preSubJob.pJob->job_index_, preSubJob.pJob, pThreadPool->running_job_number_, pThreadPool->cur_number_threads_, bNeedMoreThread);
				RSLOG_DEBUG << log_tmp;
			}			
		}
	}

final_end:
	RSLOG_DEBUG << "TPThreadPool::_prepareSubmitJobThreadProc leave...";
	return 0;
}

BOOL RThreadPool::submitJob(RBaseJob* pJob)
{
	RSLOG_DEBUG << "TPThreadPool:: prepare submit job entry...";
	TPASSERT(NULL != stop_event_);									//  如果调用 _DestroyPool后，就不能再次调用该函数

	BOOL bRet = FALSE;
	if(prepare_submit_job_mtx_ == NULL || prepare_submit_job_sempahore_ == NULL)
	{
		RSLOG_DEBUG << "TPThreadPool::prepare submit job failed leave...";
		return bRet;
	}

	if(thread_pool_submit_flag_ == FALSE)
	{
		RSLOG_DEBUG << "TPThreadPool::prepare submit job no longer receive job,leave";
		return bRet;
	}

	DWORD dw = WaitForSingleObject(prepare_submit_job_mtx_,INFINITE);
	if(dw == WAIT_OBJECT_0)
	{
		job_index_++;
		pJob->thread_pool_ = this;											//访问私有变量，并将自己赋值过去
		pJob->job_index_ = this->job_index_;								//访问私有变量，设置JobIndex
		pJob->_notifyIndex();

		PreSubmitJob preSubmitJob;
		preSubmitJob.pJob = pJob;
		preSubmitJob.pOutJobIndex = NULL;													// 内置

		LONG lPrevCount;
		BOOL fOK = ReleaseSemaphore(prepare_submit_job_sempahore_,1L,&lPrevCount);
		if(fOK)
		{
			// 加入消息队列中
			char log_tmp[100];
			sprintf_s(log_tmp,"预提交Job,lPrevCount = %d,Job = %p",lPrevCount,pJob);
			RSLOG_DEBUG << log_tmp;

			deque_prepare_submit_job_.push_back(preSubmitJob);
		}
		else
		{
			RSLOG_DEBUG << "没有预提交Job";
		}
		ReleaseMutex(prepare_submit_job_mtx_);
	}

	RSLOG_DEBUG << "TPThreadPool::prepare submit job success leave...";
	return bRet = TRUE;	
}

// cancelJob分两种，已经提交工作但是还没运行的，正在运行的
BOOL RThreadPool::cancelJob(LONG job_index,bool sync_flag)
{
	RSLOG_DEBUG << "TPThreadPool::cancelJob entry...";
	BOOL bRet = TRUE;
	BOOL bFoundPrepare = FALSE;
	BOOL bFoundWaiting = FALSE;
	BOOL bFoundDoing = FALSE;


	RSLOG_DEBUG << "job_index = " << job_index;
	if (job_index <= 0 || job_index > job_index_)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		RSLOG_DEBUG << "job_index =" << job_index << "不在job_index_范围内.";
		return FALSE;
	}

	// 首先去预提交里面去查
	DWORD dw = WaitForSingleObject(prepare_submit_job_mtx_,INFINITE);
	if(dw == WAIT_OBJECT_0)
	{
		Deque_Prepare_Submit_Job::iterator iterPreSubJob = deque_prepare_submit_job_.begin();
		for(;iterPreSubJob != deque_prepare_submit_job_.end();iterPreSubJob++)
		{
			RBaseJob* pJob = (*iterPreSubJob).pJob;
			if(pJob && pJob->getJobIndex() == job_index)
			{
				DWORD dwResult = WaitForSingleObject(prepare_submit_job_sempahore_, TP_MAX_THREAD_DEADLINE_CHECK); 
				API_VERIFY(dwResult == WAIT_OBJECT_0);
				_notifyJobCancel(pJob);
				pJob->_onCancelJob();					// 释放JOB
				_addClearJobDeque(job_index,(*iterPreSubJob).pJob);
				deque_prepare_submit_job_.erase(iterPreSubJob);
				RSLOG_DEBUG << "在预提交找到Job";

				bFoundPrepare = TRUE;
				break;
			}
		}
		ReleaseMutex(prepare_submit_job_mtx_);
	}


	if(!bFoundPrepare)
	{
		//首先查找未启动的任务 -- 因为传入的参数中没有Priority的信息，无法快速查找。
		//因此采用遍历的方式 -- 用 boost::multi_index(依赖太大)
		CFAutoLock<RLockObject> locker(&waiting_jobs_lock_);
		for (WaitingJobContainer::iterator iterWaiting = set_waiting_jobs_.begin();
			iterWaiting != set_waiting_jobs_.end();
			++iterWaiting)
		{
			if ((*iterWaiting)->getJobIndex() == job_index)
			{
				//找到,说明这个Job还没有启动
				bFoundWaiting = TRUE;

				DWORD dwResult = WaitForSingleObject(job_needed_to_do_semaphore_, INFINITE);			//释放对应的信标对象，避免个数不匹配
				TPASSERT(dwResult == WAIT_OBJECT_0);

				RBaseJob* pJob = *iterWaiting;
				TPASSERT(pJob);
				TPASSERT(pJob->getJobIndex() == job_index);
				RSLOG_DEBUG << "在 waiting job 中都找到啦";
				_notifyJobCancel(pJob);
				pJob->_onCancelJob();

				// 添加释放job队列
				_addClearJobDeque(job_index,pJob);

				set_waiting_jobs_.erase(iterWaiting);
				break;
			}
		}
	}
	else
	{
		goto cancel_job_final;
	}

	if (!bFoundWaiting)
	{
		//查找正在运行的任务
		RBaseJob* pJob = NULL;
		{
			CFAutoLock<RLockObject> locker(&doing_jobs_lock_);
			MapDoingJobContainer::iterator iterDoing = map_doing_hobs_.find(job_index);
			if (iterDoing != map_doing_hobs_.end())
			{
				bFoundDoing = TRUE;
				RSLOG_DEBUG << "在doing job 中都找到啦";
				pJob = iterDoing->second;
				TPASSERT(pJob);
				TPASSERT(pJob->getJobIndex() == job_index);
			}
		}
		if(pJob == NULL)
		{
			RSLOG_DEBUG << "job is NULL";
		}
		else
		{
			// 注意：这里只是请求Cancel，实际上任务是否能真正Cancel，需要依赖Job的实现，
			pJob->requestCancel();				// 也就是把Stop event激活
			// 不要 map_doing_hobs_.erase(iterDoing) -- Job 结束后会 erase
			if(sync_flag == true)
			{
				DWORD ret_cancel = WaitForSingleObject(pJob->get_cancel_job_event(),INFINITE);
				if (ret_cancel == WAIT_OBJECT_0)
				{
					RSLOG_DEBUG << "wait cancel job succ";
				}
				else
				{
					RSLOG_DEBUG << "wait cancel job failed";
				}
			}
		}
	}

	if (!bFoundPrepare && !bFoundWaiting && !bFoundDoing)
	{
		// Waiting 和 Doing 中都没有找到，已经执行完毕
		RSLOG_DEBUG << "在prepare job,waiting job 和 doing job 中都没找到";
	}


cancel_job_final:
	RSLOG_DEBUG << "TPThreadPool::cancelJob leave...";
	return bRet;
}

// STOP是否有信号
BOOL RThreadPool::hadRequestStop() const
{
	RSLOG_DEBUG << "TPThreadPool::hadRequestStop entry...";

	TPASSERT(NULL != stop_event_);
	BOOL bRet = (WaitForSingleObject(stop_event_, 0) == WAIT_OBJECT_0);

	RSLOG_DEBUG << "TPThreadPool::hadRequestStop leave...,bRet = "<< bRet;
	return bRet;
}

// continue是否有信号
BOOL RThreadPool::hadRequestPause() const
{
	RSLOG_DEBUG << "TPThreadPool::hadRequestPause entry...";

	BOOL bRet = (WaitForSingleObject(continue_event_, 0) == WAIT_TIMEOUT);

	RSLOG_DEBUG << "TPThreadPool::hadRequestPause leave,bRet = " << bRet;

	return bRet;
}

void RThreadPool::_destroyPool()
{
	BOOL bRet = FALSE;
	RSLOG_DEBUG << "TPThreadPool::_destroyPool entry...";

	API_VERIFY(_clearUndoWork());

	SAFE_CLOSE_HANDLE(job_needed_to_do_semaphore_,NULL);
	SAFE_CLOSE_HANDLE(subtract_thread_semaphore_,NULL);
	SAFE_CLOSE_HANDLE(all_thread_complete_event_, NULL);
	SAFE_CLOSE_HANDLE(continue_event_,NULL);
	SAFE_CLOSE_HANDLE(stop_event_,NULL);

	RSLOG_DEBUG << "TPThreadPool::_destroyPool leave...";
}

RGetJobType RThreadPool::_getJob(RBaseJob** ppJob)
{
	RSLOG_DEBUG << "TPThreadPool::_getJob entry...";

	HANDLE hWaitHandles[] = 
	{
		//TODO: 优先响应 m_hSemaphoreJobToDo 还是 m_hSemaphoreSubtractThread ?
		//  1.优先响应 m_hSemaphoreJobToDo 可以避免线程的波动
		//  2.优先响应 m_hSemaphoreSubtractThread 可以优先满足用户手动要求减少线程的需求(虽然目前尚未提供该接口)
		stop_event_,							//user stop thread pool
		job_needed_to_do_semaphore_,			//there are waiting jobs
		subtract_thread_semaphore_,				//need subtract thread
	};

	DWORD dwResult = WaitForMultipleObjects(_countof(hWaitHandles), hWaitHandles, FALSE, INFINITE);
	switch(dwResult)
	{
	case WAIT_OBJECT_0:				// stop_event_
		RSLOG_DEBUG << "TPThreadPool::_getJob leave,typGetJob = " << typeStop;
		return typeStop;
	case WAIT_OBJECT_0 + 1:			// job_needed_to_do_semaphore_
		RSLOG_DEBUG << "TPThreadPool::_getJob leave,typGetJob = " << typeGetJob;
		break;
	case WAIT_OBJECT_0 + 2:			// subtract_thread_semaphore_
		RSLOG_DEBUG << "TPThreadPool::_getJob leave,typGetJob = " << typeSubtractThread;
		return typeSubtractThread;
	default:
		TPASSERT(FALSE);
		RSLOG_DEBUG<<"TPThreadPool::_getJob leave,typGetJob = "<<typeStop;
		return typeStop;
	}

	{
		// 从等待容器中获取用户作业
		CFAutoLock<RLockObject> lockerWating(&waiting_jobs_lock_);
		TPASSERT(!set_waiting_jobs_.empty());
		WaitingJobContainer::iterator iterBegin = set_waiting_jobs_.begin();

		RBaseJob* pJob = *iterBegin;
		TPASSERT(pJob);

		*ppJob = pJob;
		set_waiting_jobs_.erase(iterBegin);
		RSLOG_DEBUG << "取到正在等待的工作的job";
		{
			// 放到进行作业的容器中
			CFAutoLock<RLockObject> lockerDoing(&doing_jobs_lock_);
			map_doing_hobs_.insert(MapDoingJobContainer::value_type(pJob->getJobIndex(), pJob));			
		}
	}

	RSLOG_DEBUG << "TPThreadPool::_getJob leave,typGetJob = " << typeGetJob;

	return typeGetJob;	
}

void RThreadPool::_doJobs()
{
	BOOL bRet = FALSE;
	RSLOG_DEBUG << "TPThreadPool::_doJobs entry...";

	RBaseJob* pJob = NULL;
	RGetJobType getJobType = typeStop;

	// 一直等job的事件进来
	while(typeGetJob == (getJobType = _getJob(&pJob)))
	{
		InterlockedIncrement(&running_job_number_);
		int nJobIndex = pJob->getJobIndex();
		RSLOG_DEBUG << "TPThreadPool Begin Run Job" <<nJobIndex;

		// API_VERIFY(pJob->_initialize());
		// if (bRet)
		{
			// 这个地方的设计和实现不是很好，是否有更好的方法?
			TPASSERT(NULL == pJob->stop_job_event_);
			pJob->stop_job_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);

			_notifyJobBegin(pJob);
			pJob->_run();
			_notifyJobEnd(pJob);

			SAFE_CLOSE_HANDLE(pJob->stop_job_event_, NULL);
			// 激活同步job
			pJob->signal_sync_cancel_job_event();
			pJob->_finalize();
		}
		InterlockedDecrement(&running_job_number_);

		RSLOG_DEBUG<<"TPThreadPool End Run Job "<<nJobIndex;
		{
			// Job结束，首先从运行列表中删除
			CFAutoLock<RLockObject> lockerDoing(&doing_jobs_lock_);
			MapDoingJobContainer::iterator iter = map_doing_hobs_.find(nJobIndex);
			TPASSERT(map_doing_hobs_.end() != iter);
			if (map_doing_hobs_.end() != iter)
			{
				// 添加到job清理队列
				_addClearJobDeque(nJobIndex, pJob);
				map_doing_hobs_.erase(iter);
			}
		}

		// 检查一下是否需要减少线程,算法不是很好
		BOOL bNeedSubtractThread = FALSE;
		{
			CFAutoLock<RLockObject> locker(&waiting_jobs_lock_);
			CFAutoLock<RLockObject> locker2(&threads_lock_);
			// 当队列中没有Job，并且当前线程数大于最小线程数时
			bNeedSubtractThread = (set_waiting_jobs_.empty() && (cur_number_threads_ > min_number_threads_) && !hadRequestStop());
			if (bNeedSubtractThread)
			{
				// 通知减少一个线程
				ReleaseSemaphore(subtract_thread_semaphore_, 1L, NULL);
			}
		}
	}
	if (typeSubtractThread == getJobType)			// 需要减少线程,应该把自己退出 -- 注意：通知退出的线程和实际退出的线程可能不是同一个
	{
		CFAutoLock<RLockObject> locker(&threads_lock_);
		LONG index = 0;
		DWORD dwCurrentThreadId = GetCurrentThreadId();
		for (; index < cur_number_threads_; index++)
		{
			if (job_thread_ids_[index] == dwCurrentThreadId)  //找到自己线程对应的位置
			{
				break;
			}
		}
		TPASSERT(index < cur_number_threads_);
		if (index < cur_number_threads_)
		{
			// 把最后一个线程的信息移到退出的线程位置 -- 如果退出的线程就是最后一个时也正确
			HANDLE hOldTemp = job_thread_handles_[index];
			job_thread_handles_[index] = job_thread_handles_[cur_number_threads_ - 1];
			job_thread_handles_[cur_number_threads_ - 1] = NULL;

			job_thread_ids_[index] = job_thread_ids_[cur_number_threads_ - 1];
			job_thread_ids_[cur_number_threads_ - 1] = 0;

			cur_number_threads_--;
			CloseHandle(hOldTemp);
			hOldTemp = NULL;

			char log_tmp[200];
			sprintf_s(log_tmp,"TPThreadPool Subtract a thread, thread id = %d(0x%x), curThreadNum = %d",
				dwCurrentThreadId, dwCurrentThreadId, cur_number_threads_);
			RSLOG_DEBUG<<log_tmp;
		}
	}
	else //typeStop
	{
		//Do Nothing
		RSLOG_DEBUG<<"typeStop";
	}

	RSLOG_DEBUG<<"TPThreadPool::_doJobs leave...";
}

//////////////////////////////////////////////////////////////////////////
// 线程执行函数
unsigned int RThreadPool::_jobThreadProc(void *pThis)
{
	RSLOG_DEBUG<<"TPThreadPool::_jobThreadProc entry...";

	RThreadPool* pThreadPool = (RThreadPool*)pThis;

	LONG nRunningNumber = InterlockedIncrement(&pThreadPool->running_thread_number_);

	pThreadPool->_doJobs();

	nRunningNumber = InterlockedDecrement(&pThreadPool->running_thread_number_);
	if (0 == nRunningNumber)
	{
		// 线程结束后判断是否是最后一个线程，如果是，激发事件
		SetEvent(pThreadPool->all_thread_complete_event_);
	}

	RSLOG_DEBUG << "TPThreadPool::_jobThreadProc leave";
	return(0);
}


//////////////////////////////////////////////////////////////////////////
// 执行清理JOB线程函数
unsigned int RThreadPool::_clearJobThreadProc(void* pParam)
{
	RSLOG_DEBUG<<"TPThreadPool::_clearJobThreadProc entry...";

	RThreadPool* pThreadPool = (RThreadPool*) pParam;
	while(TRUE)
	{
		BOOL bOK = WaitForMultipleObjects(_countof(pThreadPool->clear_job_handle_arr),
			pThreadPool->clear_job_handle_arr,TRUE,INFINITE) == WAIT_OBJECT_0;
		if(bOK)
		{
			ClearJob clearJob;
			clearJob = pThreadPool->deque_clear_job_.front();
			pThreadPool->deque_clear_job_.pop_front();
			ReleaseMutex(pThreadPool->clear_job_mtx_);

			RSLOG_DEBUG<<"TPThreadPool::_clearJobThreadProc start submit wait job (index = " << clearJob.job_index << ")";

			if(clearJob.pClearJob == NULL && clearJob.job_index == -1)
			{
				RSLOG_DEBUG<<"TPThreadPool::_clearJobThreadProc clear all end";
				goto final_end;
			}

			// 清理工作
			delete clearJob.pClearJob;
			clearJob.pClearJob = NULL;
		}
	}

final_end:
	RSLOG_DEBUG << "TPThreadPool::_clearJobThreadProc leave...";
	return 0;
}

//////////////////////////////////////////////////////////////////////////
// 添加清理job队列
BOOL RThreadPool::_addClearJobDeque(int job_index,RBaseJob* pBaseJob)
{
	RSLOG_DEBUG << "TPThreadPool::_addClearJobDeque entry";
	BOOL bRet = FALSE;
	{
		DWORD dw = WaitForSingleObject(clear_job_mtx_,INFINITE);
		if(dw == WAIT_OBJECT_0)
		{
			ClearJob cljob;
			cljob.job_index = job_index;
			cljob.pClearJob = pBaseJob;
			LONG lPrevCount;
			BOOL fOK = ReleaseSemaphore(clear_job_sempahore_,1L,&lPrevCount);
			if(fOK)
			{
				// 加入消息队列中
				char log_tmp[100];
				sprintf_s(log_tmp,"清理Job,lPrevCount = %d,index = %d",lPrevCount,job_index);
				RSLOG_DEBUG<<log_tmp;

				deque_clear_job_.push_back(cljob);
			}
			else
			{
				RSLOG_DEBUG << "没有提交清理成功Job";
			}
			ReleaseMutex(clear_job_mtx_);
		}
	}

	RSLOG_DEBUG << "TPThreadPool::_addClearJobDeque leave, bRet = " << bRet;
	return bRet;
}

//////////////////////////////////////////////////////////////////////////
// 提交退出清理线程job
void RThreadPool::_submitExitClearThreadJob()
{
	RSLOG_DEBUG << "TPThreadPool:: _submitExitClearThreadJob entry...";

	BOOL bRet = FALSE;

	DWORD dw = WaitForSingleObject(clear_job_mtx_,INFINITE);
	if(dw == WAIT_OBJECT_0)
	{
		ClearJob clj;
		clj.pClearJob = NULL;
		clj.job_index = -1;

		LONG lPrevCount;
		BOOL fOK = ReleaseSemaphore(clear_job_sempahore_,1L,&lPrevCount);
		if(fOK)
		{
			// 加入消息队列中
			char log_tmp[100];
			sprintf_s(log_tmp,"提交退出清理线程Job,lPrevCount = %d",lPrevCount);
			RSLOG_DEBUG<<log_tmp;

			deque_clear_job_.push_back(clj);
		}
		else
		{
			RSLOG_DEBUG<<"没有提交清理线程Job成功";
		}
		ReleaseMutex(clear_job_mtx_);
	}

	RSLOG_DEBUG << "TPThreadPool::_submitExitClearThreadJob leave...";
}



//////////////////////////////////////////////////////////////////////////
void RThreadPool::_notifyJobBegin(RBaseJob* pJob)
{
	RSLOG_DEBUG << "TPThreadPool::_notifyJobBegin entry...";
	TPASSERT(pJob);
	if (pJob && m_pCallBack)
	{
		m_pCallBack->onJobBegin(pJob->getJobIndex(), pJob);
	}
	RSLOG_DEBUG << "TPThreadPool::_notifyJobBegin leave...";
}

void RThreadPool::_notifyJobEnd(RBaseJob* pJob)
{
	RSLOG_DEBUG << "TPThreadPool::_notifyJobEnd entry...";
	TPASSERT(pJob);
	if (pJob && m_pCallBack)
	{
		m_pCallBack->onJobEnd(pJob->getJobIndex(), pJob);
	}
	RSLOG_DEBUG << "TPThreadPool::_notifyJobEnd leave...";
}

void RThreadPool::_notifyJobCancel(RBaseJob* pJob)
{
	RSLOG_DEBUG << "TPThreadPool::_notifyJobCancel entry...";
	TPASSERT(pJob);
	if (pJob && m_pCallBack)
	{
		m_pCallBack->onJobCancel(pJob->getJobIndex(), pJob);
	}
	RSLOG_DEBUG << "TPThreadPool::_notifyJobCancel leave...";
}

void RThreadPool::_notifyJobProgress(RBaseJob* pJob, LONG64 nCurPos, LONG64 nTotalSize)
{
	RSLOG_DEBUG << "TPThreadPool::_notifyJobProgress entry...";
	TPASSERT(pJob);
	if (pJob && m_pCallBack)
	{
		m_pCallBack->onJobProgress(pJob->getJobIndex(), pJob, nCurPos, nTotalSize);
	}
	RSLOG_DEBUG << "TPThreadPool::_notifyJobProgress leave...";
}

void RThreadPool::_notifyJobError(RBaseJob* pJob, DWORD dwError, LPCTSTR pszDescription)
{
	RSLOG_DEBUG << "TPThreadPool::_notifyJobError entry...";
	TPASSERT(pJob);
	if (pJob && m_pCallBack)
	{
		m_pCallBack->onJobError(pJob->getJobIndex(), pJob, dwError, pszDescription);
	}
	RSLOG_DEBUG << "TPThreadPool::_notifyJobError leave...";
}
 