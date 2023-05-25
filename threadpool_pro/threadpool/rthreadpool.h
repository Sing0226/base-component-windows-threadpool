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
		typeError,					//����δ֪����

	}RGetJobType;


	class RThreadPool
	{
		friend class RBaseJob;  //����Job�� GetJobWaitType �л�ȡ m_hEventStop/m_hEventContinue

	public:

		RThreadPool(RThreadPoolCallBack* pCallBack = NULL);
		virtual ~RThreadPool(void);

		// ��ʼ�̳߳�,��ʱ�ᴴ�� min_number_threads ���̣߳�
		// Ȼ�������������� min_number_threads -- max_number_threads
		// ֮�����е����̵߳ĸ���
		BOOL start(LONG min_number_threads, LONG max_number_threads);

		// ����ֹͣ�̳߳�
		// ע�⣺
		//   1.ֻ������StopEvent����ҪJob����GetJobWaitType���� 
		//   2.���������ǰע��ĵ���δ���еĹ����������Ҫɾ������Ҫ����ClearUndoWork
		BOOL stop(DWORD dwTimeOut  = TP_MAX_THREAD_DEADLINE_CHECK);

		// ���̳߳���ע�Ṥ�� -- �����ǰû�п��е��̣߳����ҵ�ǰ�߳���С������߳���������Զ������µ��̣߳�
		// �ɹ����ͨ�� outJobIndex ����Job�������ţ���ͨ����������λ��ȡ���ض���Job
		BOOL submitJob(RBaseJob* pJob);

		//! ȡ��ָ����Job,
		//! TODO:���ȡ��Job���ͻ������ܵ����ߵõ�ָ��ʱ��Jobִ����� delete this�����ճ�Ұָ���쳣
		//��sync_flag ͬ����־��Ĭ�����첽��
		BOOL cancelJob(LONG job_index,bool sync_flag = false);

		//BOOL PauseJob(LONG nJobIndex);
		//BOOL ResumeJob(LONG nJobIndex);

		// ������ͣ�̳߳صĲ�������ʱû��
		BOOL pause();

		// ��������̳߳صĲ�������ʱû��
		BOOL resume();

		// �Ƿ��Ѿ���������ͣ�̳߳�
		BOOL hadRequestPause() const;

		// �Ƿ��Ѿ�������ֹͣ�̳߳�
		BOOL hadRequestStop() const;

	public:

		// ���θ�ֵ����Ϳ�������
		DISABLE_COPY_AND_ASSIGNMENT(RThreadPool);

	protected:

		// �������е��߳�,��� ��ǰ�߳��� 
		// + thread_num <= max_thread_num_ ʱ ��ɹ�ִ��
		BOOL _addJobThread(int thread_num);

		// �����ǰδ��ɵĹ�����
		BOOL _clearUndoWork();

		// BOOL _stopAndWait(DWORD dwTimeOut = TP_MAX_THREAD_DEADLINE_CHECK);

		// �ȴ������̶߳��������ͷ�Start�з�����߳���Դ
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
		// �ύ�˳�Ԥ�ύ��ҵ
		void _submitPrepareExitThreadJob();

		// �����ǰԤ�ύ������
		BOOL _clearPrepareSubmtJobs();

		// ��ӵ�����job������
		BOOL _addClearJobDeque(int job_index, RBaseJob* pBaseJob);

		// �ύ�˳�����job
		void _submitExitClearThreadJob();

	private:

		BOOL thread_pool_is_running_;

		LONG min_number_threads_;
		LONG max_number_threads_;

		RThreadPoolCallBack* m_pCallBack;		 // �ص��ӿ�

		LONG job_index_;							 // Job��������ÿ SubmitJob һ�Σ������1

		LONG running_job_number_;				 // ��ǰ�������е�Job����

		//TODO: �������ͳһ��
		LONG cur_number_threads_;                 // ��ǰ���̸߳���(��Ҫ����ά�� cur_number_threads_ ����)
		LONG running_thread_number_;				 // ��ǰ�����ŵ��̸߳���(���������е��߳̽���ʱ���� Complete �¼�)


		HANDLE* job_thread_handles_;			 // �����߳̾��������
		DWORD*  job_thread_ids_;                 // �����߳� Id ������(Ϊ�����߳̽�������������е�λ��)


		// ����ȴ�Job����Ϣ�����������ȼ������⣬����һ���Ǵ���ǰ�濪ʼȡ����˱���� set��
		// ��֤���ȼ��ߡ�JobIndexС(ͬ���ȼ�ʱFIFO) ��Job����ǰ��
		typedef UnreferenceLess<RBaseJob* >	JobBaseUnreferenceLess;
		typedef std::set<RBaseJob*, JobBaseUnreferenceLess > WaitingJobContainer;
		WaitingJobContainer		set_waiting_jobs_;									// �ȴ����е�Job


		// ��������Job����Ϣ�� ���ڻ�Ƶ�����롢ɾ��������Ҫ����JobIndex���ң���˱���� map
		typedef std::map<int, RBaseJob*>	MapDoingJobContainer;
		MapDoingJobContainer		map_doing_hobs_;					// �������е�Job


		HANDLE stop_event_;								// ֹͣPool���¼�
		HANDLE all_thread_complete_event_;				// ���е��̶߳�����ʱ��������¼�
		HANDLE continue_event_;							// ����Pool�������е��¼�
		HANDLE job_needed_to_do_semaphore_;				// ���滹�ж��ٸ�Job���ź���,ÿSubmitһ��Job,������һ��
		HANDLE subtract_thread_semaphore_;				// ���ڼ����̸߳���ʱ���ź���,��ʼʱ����Ϊ0,ÿҪ�ͷ�һ����������һ����


		RCriticalSection doing_jobs_lock_;				//���� map_doing_hobs_ ʱ����
		RCriticalSection waiting_jobs_lock_;			//���� set_waiting_jobs_ ʱ����
		RCriticalSection threads_lock_;				//���� job_thread_handles_/job_thread_ids_ ʱ����

		static unsigned int CALLBACK _jobThreadProc(void *pParam);		// �����̵߳�ִ�к���


		/////////////////////////////////////////////////////////////////////////////
		// Define PrepareSubmitJob
		typedef struct tagPreSubmitJob
		{
			RBaseJob* pJob;
			int* pOutJobIndex;
		}PreSubmitJob,*PPreSubmitJob;

		// Ԥ�ύ�������� 
		typedef std::deque<PreSubmitJob> Deque_Prepare_Submit_Job;
		Deque_Prepare_Submit_Job deque_prepare_submit_job_;

		HANDLE prepare_submit_job_thread_handle_;
		unsigned int prepare_submit_job_thread_id_;
		HANDLE prepare_submit_job_mtx_;
		HANDLE prepare_submit_job_sempahore_;
		HANDLE prepare_submit_job_handle_arr[2];

		RCriticalSection prepare_submit_job_lock_;									//Ԥ�ύjob ʱ����

		RCriticalSection thread_pool_start_work_lock_;								// �̳߳ؿ�ʼ��������ֹ�������
		static unsigned int CALLBACK _prepareSubmitJobThreadProc(void* pParam);		// �ύjob�̵߳�ִ�к���

		// Add by Simone 2017-06-26 �����߳�job�Զ�������
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

		// Add by Simone 2017-06-27  ����ֹͣ����job��ʶ
		BOOL thread_pool_submit_flag_;

	};

#endif

