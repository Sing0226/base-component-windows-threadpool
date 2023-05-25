#include "threadpool/rbasejob.h"
#include "threadpool/rthreadpoolcallback.h"

#include <set>
#include <map>
#include <deque>

#ifndef _R_THREAD_POOL_H__
#define _R_THREAD_POOL_H__



	typedef enum tagGetJobType
	{
		typeStop,
		typeSubtractThread,
		typeGetJob,
		typeError,					//发生未知错误

	}RGetJobType;


	class RThreadPool
	{
		friend class RBaseJob;  //允许Job在 GetJobWaitType 中获取 m_hEventStop/m_hEventContinue

	public:

		RThreadPool(RThreadPoolCallBack* pCallBack = NULL);
		virtual ~RThreadPool(void);

		// 开始线程池,此时会创建 min_number_threads 个线程，
		// 然后会根据任务数在 min_number_threads -- max_number_threads
		// 之间自行调节线程的个数
		BOOL start(LONG min_number_threads, LONG max_number_threads);

		// 请求停止线程池
		// 注意：
		//   1.只是设置StopEvent，需要Job根据GetJobWaitType处理 
		//   2.不会清除当前注册的但尚未进行的工作，如果需要删除，需要调用ClearUndoWork
		BOOL stop(DWORD dwTimeOut  = TP_MAX_THREAD_DEADLINE_CHECK);

		// 向线程池中注册工作 -- 如果当前没有空闲的线程，并且当前线程数小于最大线程数，则会自动创建新的线程，
		// 成功后会通过 outJobIndex 返回Job的索引号，可通过该索引定位、取消特定的Job
		BOOL submitJob(RBaseJob* pJob);

		//! 取消指定的Job,
		//! TODO:如果取出Job给客户，可能调用者得到指针时，Job执行完毕 delete this，会照成野指针异常
		//！sync_flag 同步标志，默认是异步的
		BOOL cancelJob(LONG job_index,bool sync_flag = false);

		//BOOL PauseJob(LONG nJobIndex);
		//BOOL ResumeJob(LONG nJobIndex);

		// 请求暂停线程池的操作，暂时没用
		BOOL pause();

		// 请求继续线程池的操作，暂时没用
		BOOL resume();

		// 是否已经请求了暂停线程池
		BOOL hadRequestPause() const;

		// 是否已经请求了停止线程池
		BOOL hadRequestStop() const;

	public:

		// 屏蔽赋值构造和拷贝构造
		DISABLE_COPY_AND_ASSIGNMENT(RThreadPool);

	protected:

		// 增加运行的线程,如果 当前线程数 
		// + thread_num <= max_thread_num_ 时 会成功执行
		BOOL _addJobThread(int thread_num);

		// 清除当前未完成的工作，
		BOOL _clearUndoWork();

		// BOOL _stopAndWait(DWORD dwTimeOut = TP_MAX_THREAD_DEADLINE_CHECK);

		// 等待所有线程都结束并释放Start中分配的线程资源
		BOOL _wait(DWORD dwTimeOut = TP_MAX_THREAD_DEADLINE_CHECK);

		void _destroyPool();
		void _doJobs();

		RGetJobType _getJob(RBaseJob** ppJob);

		void _notifyJobBegin(RBaseJob* pJob);
		void _notifyJobEnd(RBaseJob* pJob);
		void _notifyJobCancel(RBaseJob* pJob);

		void _notifyJobProgress(RBaseJob* pJob, LONG64 nCurPos, LONG64 nTotalSize);
		void _notifyJobError(RBaseJob* pJob, DWORD dwError, LPCTSTR pszDescription);
		
		//////////////////////////////////////////////////////////////////////////
		// 提交退出预提交作业
		void _submitPrepareExitThreadJob();

		// 清除当前预提交工作，
		BOOL _clearPrepareSubmtJobs();

		// 添加到清理job队列中
		BOOL _addClearJobDeque(int job_index, RBaseJob* pBaseJob);

		// 提交退出清理job
		void _submitExitClearThreadJob();

	private:

		BOOL thread_pool_is_running_;

		LONG min_number_threads_;
		LONG max_number_threads_;

		RThreadPoolCallBack* m_pCallBack;		 // 回调接口

		LONG job_index_;							 // Job的索引，每 SubmitJob 一次，则递增1

		LONG running_job_number_;				 // 当前正在运行的Job个数

		//TODO: 两个最好统一？
		LONG cur_number_threads_;                 // 当前的线程个数(主要用来维护 cur_number_threads_ 数组)
		LONG running_thread_number_;				 // 当前运行着的线程个数(用来在所有的线程结束时激发 Complete 事件)


		HANDLE* job_thread_handles_;			 // 保存线程句柄的数组
		DWORD*  job_thread_ids_;                 // 保存线程 Id 的数组(为了在线程结束后调整数组中的位置)


		// 保存等待Job的信息，由于有优先级的问题，而且一般是从最前面开始取，因此保存成 set，
		// 保证优先级高、JobIndex小(同优先级时FIFO) 的Job在最前面
		typedef UnreferenceLess<RBaseJob* >	JobBaseUnreferenceLess;
		typedef std::set<RBaseJob*, JobBaseUnreferenceLess > WaitingJobContainer;
		WaitingJobContainer		set_waiting_jobs_;									// 等待运行的Job


		// 保存运行Job的信息， 由于会频繁加入、删除，且需要按照JobIndex查找，因此保存成 map
		typedef std::map<int, RBaseJob*>	MapDoingJobContainer;
		MapDoingJobContainer		map_doing_hobs_;					// 正在运行的Job


		HANDLE stop_event_;								// 停止Pool的事件
		HANDLE all_thread_complete_event_;				// 所有的线程都结束时激发这个事件
		HANDLE continue_event_;							// 整个Pool继续运行的事件
		HANDLE job_needed_to_do_semaphore_;				// 保存还有多少个Job的信号量,每Submit一个Job,就增加一个
		HANDLE subtract_thread_semaphore_;				// 用于减少线程个数时的信号量,初始时个数为0,每要释放一个，就增加一个，


		RCriticalSection doing_jobs_lock_;				//访问 map_doing_hobs_ 时互斥
		RCriticalSection waiting_jobs_lock_;			//访问 set_waiting_jobs_ 时互斥
		RCriticalSection threads_lock_;				//访问 job_thread_handles_/job_thread_ids_ 时互斥

		static unsigned int CALLBACK _jobThreadProc(void *pParam);		// 工作线程的执行函数


		/////////////////////////////////////////////////////////////////////////////
		// Define PrepareSubmitJob
		typedef struct tagPreSubmitJob
		{
			RBaseJob* pJob;
			int* pOutJobIndex;
		}PreSubmitJob,*PPreSubmitJob;

		// 预提交工作队列 
		typedef std::deque<PreSubmitJob> Deque_Prepare_Submit_Job;
		Deque_Prepare_Submit_Job deque_prepare_submit_job_;

		HANDLE prepare_submit_job_thread_handle_;
		unsigned int prepare_submit_job_thread_id_;
		HANDLE prepare_submit_job_mtx_;
		HANDLE prepare_submit_job_sempahore_;
		HANDLE prepare_submit_job_handle_arr[2];

		RCriticalSection prepare_submit_job_lock_;									//预提交job 时互斥

		RCriticalSection thread_pool_start_work_lock_;								// 线程池开始工作锁防止启动多个
		static unsigned int CALLBACK _prepareSubmitJobThreadProc(void* pParam);		// 提交job线程的执行函数

		// Add by Simone 2017-06-26 增加线程job自动清理功能
		typedef struct tagClearJob
		{
			int job_index;
			RBaseJob* pClearJob;
		}ClearJob,*PClearJob;
		typedef std::deque<ClearJob> Deque_Clear_Job;
		Deque_Clear_Job deque_clear_job_;

		HANDLE clear_job_thread_handle_;
		unsigned int clear_job_thread_id_;
		HANDLE clear_job_mtx_;
		HANDLE clear_job_sempahore_;
		HANDLE clear_job_handle_arr[2];

		static unsigned int CALLBACK _clearJobThreadProc(void* pParam);

		// Add by Simone 2017-06-27  增加停止接收job标识
		BOOL thread_pool_submit_flag_;

	};

#endif

