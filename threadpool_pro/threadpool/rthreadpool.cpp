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

	//������ͬʱ���Ĺ���������Ϊ MAXLONG -- Ŀǰ��ʱ�����Ƕ����еĸ���
	job_needed_to_do_semaphore_ = ::CreateSemaphore(NULL, 0, MAXLONG, NULL);
	TPASSERT(NULL != job_needed_to_do_semaphore_);

	//���������̸߳������ź���
	subtract_thread_semaphore_ = CreateSemaphore(NULL, 0, MAXLONG, NULL);
	TPASSERT(NULL != subtract_thread_semaphore_);

	// Ԥ�ύjob
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

	// ���������߳�
	clear_job_thread_handle_ = (HANDLE) _beginthreadex( NULL, 0, _clearJobThreadProc, this, 0, &clear_job_thread_id_);
}


RThreadPool::~RThreadPool()
{
	BOOL bRet = FALSE;
	RSLOG_DEBUG<<"Stop all threads,wait all exit";
	API_VERIFY(stop(INFINITE));

	// �ͷ�Ԥ�ύҵ��
	_clearPrepareSubmtJobs();
	TPASSERT(NULL == prepare_submit_job_thread_handle_);	
	TPASSERT(0 == prepare_submit_job_thread_id_);	

	RSLOG_DEBUG<<"Destroy pool";
	_destroyPool();

	TPASSERT(set_waiting_jobs_.empty());
	TPASSERT(map_doing_hobs_.empty());
	TPASSERT(0 == running_thread_number_);				//����ʱ���е��̶߳�Ҫ����


	TPASSERT(FALSE == thread_pool_is_running_);	

	// �ύ�����߳�job
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
	// ��������Ԥ�ύ�߳�
	prepare_submit_job_thread_handle_ = (HANDLE) _beginthreadex( NULL, 0, _prepareSubmitJobThreadProc, this, 0, &prepare_submit_job_thread_id_);
	thread_pool_submit_flag_ = TRUE;

	RSLOG_DEBUG<<"TPThreadPool::start, ThreadNum is ["<<min_number_threads<<"-"<<max_number_threads<<"]";

	TPASSERT( 0 <= min_number_threads );
	TPASSERT( min_number_threads <= max_number_threads );       

	min_number_threads_ = min_number_threads;
	max_number_threads_ = max_number_threads;

	API_VERIFY(ResetEvent(stop_event_));
	API_VERIFY(ResetEvent(all_thread_complete_event_));
	API_VERIFY(SetEvent(continue_event_));										// ���ü����¼�����֤���������߳�������

	{
		CFAutoLock<RLockObject>   locker(&threads_lock_);						// ������addThread������
		TPASSERT(NULL == job_thread_handles_);
		if(NULL == job_thread_handles_)											//��ֹ��ε���Start
		{
			job_thread_handles_ = new HANDLE[max_number_threads_];					//����max_number_threads���̵߳Ŀռ�
			ZeroMemory(job_thread_handles_,sizeof(HANDLE) * max_number_threads_);

			job_thread_ids_ = new DWORD[max_number_threads_];
			ZeroMemory(job_thread_ids_,sizeof(DWORD) * max_number_threads_);

			_addJobThread(min_number_threads_);										//��ʼʱֻ���� min_number_threads_ ���߳�
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
		// CFAutoLock<RLockObject> locker(&threads_lock_); // ���øú���֮ǰ���д������б���
		if (cur_number_threads_ + thread_num > max_number_threads_)
		{
			TPASSERT(FALSE);
			//�����������������ټ���
			SetLastError(ERROR_INVALID_PARAMETER);
			RSLOG_DEBUG<<"���ӵ��߳�������������߳���...";
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
	TPASSERT(NULL != stop_event_);									//  ������� _DestroyPool�󣬾Ͳ����ٴε��øú���

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
			// ������Ϣ������
			char log_tmp[100];
			sprintf_s(log_tmp,"Ԥ�ύ�˳�Ԥ�ύ�߳�Job,lPrevCount = %d,Job = %p",lPrevCount,preSubmitJob.pJob);
			RSLOG_DEBUG<<log_tmp;

			// deque_prepare_submit_job_.push_front(preSubmitJob);
			deque_prepare_submit_job_.push_back(preSubmitJob);
		}
		else
		{
			RSLOG_DEBUG<<"û��Ԥ�ύJob";
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
	case WAIT_OBJECT_0:							//���е��̶߳�������
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
			//�ͷŶ�Ӧ���ű����������� m_WaitingJobs �ĸ�����һ�µ�
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

// �����ǰԤ�ύ������
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
			//�ͷŶ�Ӧ���ű����������� prepare submit jobs �ĸ�����һ�µ�
			DWORD dwResult = WaitForSingleObject(prepare_submit_job_sempahore_, TP_MAX_THREAD_DEADLINE_CHECK); 
			API_VERIFY(dwResult == WAIT_OBJECT_0);

			Deque_Prepare_Submit_Job::iterator iterPreSubJob = deque_prepare_submit_job_.begin();
			RBaseJob* pJob = (*iterPreSubJob).pJob;
			TPASSERT(pJob);
			_notifyJobCancel(pJob);
			pJob->_onCancelJob();					// �ͷ�JOB
			// ��ӵ��������
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
// Ԥ�ύ�ù����߳�ִ�к���
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

			TPASSERT(NULL != pThreadPool->stop_event_);									//  ������� _DestroyPool�󣬾Ͳ����ٴε��øú���

			RSLOG_DEBUG << "TPThreadPool::_prepareSubmitJobThreadProc start submit wait job";

			BOOL bRet = FALSE;
			if(preSubJob.pJob == NULL && preSubJob.pOutJobIndex !=NULL && *preSubJob.pOutJobIndex == -256)
			{
				delete preSubJob.pOutJobIndex;
				preSubJob.pOutJobIndex = NULL;
				goto final_end;
			}

			//����Job���һ���һ���ȴ��߳�
			{
				CFAutoLock<RLockObject> locker(&(pThreadPool->waiting_jobs_lock_));

				if (preSubJob.pOutJobIndex)
				{
					*(preSubJob.pOutJobIndex) = pThreadPool->job_index_;
				}

				pThreadPool->set_waiting_jobs_.insert(preSubJob.pJob);
				API_VERIFY(ReleaseSemaphore(pThreadPool->job_needed_to_do_semaphore_, 1L, NULL));
			}

			SwitchToThread();//���ѵȴ����̣߳�ʹ�������߳̿��Ի�ȡJob -- ע�� CFAutoLock �ķ�Χ

			{
				//	�����е��̶߳�������Jobʱ������Ҫ�����߳�  -- ���� m_nRunningJobNumber �ӱ���(ֻ�Ƕ�ȡ),��ȡ�Ļ�ҲҪ�ӱ���
				CFAutoLock<RLockObject> locker(&pThreadPool->threads_lock_);
				TPASSERT(pThreadPool->running_job_number_ <= pThreadPool->cur_number_threads_);
				BOOL bNeedMoreThread = (pThreadPool->running_job_number_ == pThreadPool->cur_number_threads_) && 
					(pThreadPool->cur_number_threads_ < pThreadPool->max_number_threads_); 
				if (bNeedMoreThread)
				{
					API_VERIFY(pThreadPool->_addJobThread(1L));				// ÿ������һ���߳�
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
	TPASSERT(NULL != stop_event_);									//  ������� _DestroyPool�󣬾Ͳ����ٴε��øú���

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
		pJob->thread_pool_ = this;											//����˽�б����������Լ���ֵ��ȥ
		pJob->job_index_ = this->job_index_;								//����˽�б���������JobIndex
		pJob->_notifyIndex();

		PreSubmitJob preSubmitJob;
		preSubmitJob.pJob = pJob;
		preSubmitJob.pOutJobIndex = NULL;													// ����

		LONG lPrevCount;
		BOOL fOK = ReleaseSemaphore(prepare_submit_job_sempahore_,1L,&lPrevCount);
		if(fOK)
		{
			// ������Ϣ������
			char log_tmp[100];
			sprintf_s(log_tmp,"Ԥ�ύJob,lPrevCount = %d,Job = %p",lPrevCount,pJob);
			RSLOG_DEBUG << log_tmp;

			deque_prepare_submit_job_.push_back(preSubmitJob);
		}
		else
		{
			RSLOG_DEBUG << "û��Ԥ�ύJob";
		}
		ReleaseMutex(prepare_submit_job_mtx_);
	}

	RSLOG_DEBUG << "TPThreadPool::prepare submit job success leave...";
	return bRet = TRUE;	
}

// cancelJob�����֣��Ѿ��ύ�������ǻ�û���еģ��������е�
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
		RSLOG_DEBUG << "job_index =" << job_index << "����job_index_��Χ��.";
		return FALSE;
	}

	// ����ȥԤ�ύ����ȥ��
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
				pJob->_onCancelJob();					// �ͷ�JOB
				_addClearJobDeque(job_index,(*iterPreSubJob).pJob);
				deque_prepare_submit_job_.erase(iterPreSubJob);
				RSLOG_DEBUG << "��Ԥ�ύ�ҵ�Job";

				bFoundPrepare = TRUE;
				break;
			}
		}
		ReleaseMutex(prepare_submit_job_mtx_);
	}


	if(!bFoundPrepare)
	{
		//���Ȳ���δ���������� -- ��Ϊ����Ĳ�����û��Priority����Ϣ���޷����ٲ��ҡ�
		//��˲��ñ����ķ�ʽ -- �� boost::multi_index(����̫��)
		CFAutoLock<RLockObject> locker(&waiting_jobs_lock_);
		for (WaitingJobContainer::iterator iterWaiting = set_waiting_jobs_.begin();
			iterWaiting != set_waiting_jobs_.end();
			++iterWaiting)
		{
			if ((*iterWaiting)->getJobIndex() == job_index)
			{
				//�ҵ�,˵�����Job��û������
				bFoundWaiting = TRUE;

				DWORD dwResult = WaitForSingleObject(job_needed_to_do_semaphore_, INFINITE);			//�ͷŶ�Ӧ���ű���󣬱��������ƥ��
				TPASSERT(dwResult == WAIT_OBJECT_0);

				RBaseJob* pJob = *iterWaiting;
				TPASSERT(pJob);
				TPASSERT(pJob->getJobIndex() == job_index);
				RSLOG_DEBUG << "�� waiting job �ж��ҵ���";
				_notifyJobCancel(pJob);
				pJob->_onCancelJob();

				// ����ͷ�job����
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
		//�����������е�����
		RBaseJob* pJob = NULL;
		{
			CFAutoLock<RLockObject> locker(&doing_jobs_lock_);
			MapDoingJobContainer::iterator iterDoing = map_doing_hobs_.find(job_index);
			if (iterDoing != map_doing_hobs_.end())
			{
				bFoundDoing = TRUE;
				RSLOG_DEBUG << "��doing job �ж��ҵ���";
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
			// ע�⣺����ֻ������Cancel��ʵ���������Ƿ�������Cancel����Ҫ����Job��ʵ�֣�
			pJob->requestCancel();				// Ҳ���ǰ�Stop event����
			// ��Ҫ map_doing_hobs_.erase(iterDoing) -- Job ������� erase
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
		// Waiting �� Doing �ж�û���ҵ����Ѿ�ִ�����
		RSLOG_DEBUG << "��prepare job,waiting job �� doing job �ж�û�ҵ�";
	}


cancel_job_final:
	RSLOG_DEBUG << "TPThreadPool::cancelJob leave...";
	return bRet;
}

// STOP�Ƿ����ź�
BOOL RThreadPool::hadRequestStop() const
{
	RSLOG_DEBUG << "TPThreadPool::hadRequestStop entry...";

	TPASSERT(NULL != stop_event_);
	BOOL bRet = (WaitForSingleObject(stop_event_, 0) == WAIT_OBJECT_0);

	RSLOG_DEBUG << "TPThreadPool::hadRequestStop leave...,bRet = "<< bRet;
	return bRet;
}

// continue�Ƿ����ź�
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
		//TODO: ������Ӧ m_hSemaphoreJobToDo ���� m_hSemaphoreSubtractThread ?
		//  1.������Ӧ m_hSemaphoreJobToDo ���Ա����̵߳Ĳ���
		//  2.������Ӧ m_hSemaphoreSubtractThread �������������û��ֶ�Ҫ������̵߳�����(��ȻĿǰ��δ�ṩ�ýӿ�)
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
		// �ӵȴ������л�ȡ�û���ҵ
		CFAutoLock<RLockObject> lockerWating(&waiting_jobs_lock_);
		TPASSERT(!set_waiting_jobs_.empty());
		WaitingJobContainer::iterator iterBegin = set_waiting_jobs_.begin();

		RBaseJob* pJob = *iterBegin;
		TPASSERT(pJob);

		*ppJob = pJob;
		set_waiting_jobs_.erase(iterBegin);
		RSLOG_DEBUG << "ȡ�����ڵȴ��Ĺ�����job";
		{
			// �ŵ�������ҵ��������
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

	// һֱ��job���¼�����
	while(typeGetJob == (getJobType = _getJob(&pJob)))
	{
		InterlockedIncrement(&running_job_number_);
		int nJobIndex = pJob->getJobIndex();
		RSLOG_DEBUG << "TPThreadPool Begin Run Job" <<nJobIndex;

		// API_VERIFY(pJob->_initialize());
		// if (bRet)
		{
			// ����ط�����ƺ�ʵ�ֲ��Ǻܺã��Ƿ��и��õķ���?
			TPASSERT(NULL == pJob->stop_job_event_);
			pJob->stop_job_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);

			_notifyJobBegin(pJob);
			pJob->_run();
			_notifyJobEnd(pJob);

			SAFE_CLOSE_HANDLE(pJob->stop_job_event_, NULL);
			// ����ͬ��job
			pJob->signal_sync_cancel_job_event();
			pJob->_finalize();
		}
		InterlockedDecrement(&running_job_number_);

		RSLOG_DEBUG<<"TPThreadPool End Run Job "<<nJobIndex;
		{
			// Job���������ȴ������б���ɾ��
			CFAutoLock<RLockObject> lockerDoing(&doing_jobs_lock_);
			MapDoingJobContainer::iterator iter = map_doing_hobs_.find(nJobIndex);
			TPASSERT(map_doing_hobs_.end() != iter);
			if (map_doing_hobs_.end() != iter)
			{
				// ��ӵ�job�������
				_addClearJobDeque(nJobIndex, pJob);
				map_doing_hobs_.erase(iter);
			}
		}

		// ���һ���Ƿ���Ҫ�����߳�,�㷨���Ǻܺ�
		BOOL bNeedSubtractThread = FALSE;
		{
			CFAutoLock<RLockObject> locker(&waiting_jobs_lock_);
			CFAutoLock<RLockObject> locker2(&threads_lock_);
			// ��������û��Job�����ҵ�ǰ�߳���������С�߳���ʱ
			bNeedSubtractThread = (set_waiting_jobs_.empty() && (cur_number_threads_ > min_number_threads_) && !hadRequestStop());
			if (bNeedSubtractThread)
			{
				// ֪ͨ����һ���߳�
				ReleaseSemaphore(subtract_thread_semaphore_, 1L, NULL);
			}
		}
	}
	if (typeSubtractThread == getJobType)			// ��Ҫ�����߳�,Ӧ�ð��Լ��˳� -- ע�⣺֪ͨ�˳����̺߳�ʵ���˳����߳̿��ܲ���ͬһ��
	{
		CFAutoLock<RLockObject> locker(&threads_lock_);
		LONG index = 0;
		DWORD dwCurrentThreadId = GetCurrentThreadId();
		for (; index < cur_number_threads_; index++)
		{
			if (job_thread_ids_[index] == dwCurrentThreadId)  //�ҵ��Լ��̶߳�Ӧ��λ��
			{
				break;
			}
		}
		TPASSERT(index < cur_number_threads_);
		if (index < cur_number_threads_)
		{
			// �����һ���̵߳���Ϣ�Ƶ��˳����߳�λ�� -- ����˳����߳̾������һ��ʱҲ��ȷ
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
// �߳�ִ�к���
unsigned int RThreadPool::_jobThreadProc(void *pThis)
{
	RSLOG_DEBUG<<"TPThreadPool::_jobThreadProc entry...";

	RThreadPool* pThreadPool = (RThreadPool*)pThis;

	LONG nRunningNumber = InterlockedIncrement(&pThreadPool->running_thread_number_);

	pThreadPool->_doJobs();

	nRunningNumber = InterlockedDecrement(&pThreadPool->running_thread_number_);
	if (0 == nRunningNumber)
	{
		// �߳̽������ж��Ƿ������һ���̣߳�����ǣ������¼�
		SetEvent(pThreadPool->all_thread_complete_event_);
	}

	RSLOG_DEBUG << "TPThreadPool::_jobThreadProc leave";
	return(0);
}


//////////////////////////////////////////////////////////////////////////
// ִ������JOB�̺߳���
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

			// ������
			delete clearJob.pClearJob;
			clearJob.pClearJob = NULL;
		}
	}

final_end:
	RSLOG_DEBUG << "TPThreadPool::_clearJobThreadProc leave...";
	return 0;
}

//////////////////////////////////////////////////////////////////////////
// �������job����
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
				// ������Ϣ������
				char log_tmp[100];
				sprintf_s(log_tmp,"����Job,lPrevCount = %d,index = %d",lPrevCount,job_index);
				RSLOG_DEBUG<<log_tmp;

				deque_clear_job_.push_back(cljob);
			}
			else
			{
				RSLOG_DEBUG << "û���ύ����ɹ�Job";
			}
			ReleaseMutex(clear_job_mtx_);
		}
	}

	RSLOG_DEBUG << "TPThreadPool::_addClearJobDeque leave, bRet = " << bRet;
	return bRet;
}

//////////////////////////////////////////////////////////////////////////
// �ύ�˳������߳�job
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
			// ������Ϣ������
			char log_tmp[100];
			sprintf_s(log_tmp,"�ύ�˳������߳�Job,lPrevCount = %d",lPrevCount);
			RSLOG_DEBUG<<log_tmp;

			deque_clear_job_.push_back(clj);
		}
		else
		{
			RSLOG_DEBUG<<"û���ύ�����߳�Job�ɹ�";
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
 