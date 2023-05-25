/////////////////////////////////////////////////////////////////////////////////
/// Copyright (C), 2022, 
/// Rayshape Corporation. All rights reserved.
/// @file    rbasebob.h
/// @author  Simone
/// @date    2023/03/28
/// @brief   
///
/// @history v0.01 2015/11/07  ��Ԫ����
/////////////////////////////////////////////////////////////////////////////////
///

#ifndef _R_BASE_JOB_H__
#define _R_BASE_JOB_H__

#include <Windows.h>
#include "threadpool/rthreadpoolbase.h"

//  ǰ��������
//! ���������ص㣺
//  1.���Զ�����������̵߳Ķ����� ��С/��� �̸߳���֮�����
//  2.�ܷ���ĶԵ����������ȡ������������δ�������ɿ�ܴ��봦���������Ѿ����У�����Ҫ JobBase ��������� GetJobWaitType �ķ���ֵ���д���
//  3.�ܶ������̳߳ؽ��� ��ͣ��������ֹͣ ���� -- ��Ҫ JobBase ��������� GetJobWaitType �ķ���ֵ���д���
//  4.֧�ֻص���ʽ�ķ���֪ͨ( Progress/Error ��)
//  5.ʹ�õ���΢��Ļ���API����֧��WinXP��Vista��Win7�ȸ��ֲ���ϵͳ
class RThreadPool;


//////////////////////////////////////////////////////////////////////////
// ����Job����,���ڿ�����ģ����Ϊ����Ҳ�У�����ͽ����ŷ�ȥʵ�ְɡ�

class RBaseJob
{
	friend class RThreadPool;   //����Threadpool���� m_pThreadPool/m_nJobIndex ��ֵ

	template <typename T> friend struct UnreferenceLess;

public:

	RBaseJob(int job_priority = 0);
	virtual ~RBaseJob();

	//! �Ƚ�Job�����ȼ���С������ȷ���� Waiting �����еĶ��У� ��������Ϊ Priority -> Index
	bool operator < (const RBaseJob& other) const;

	int getJobIndex() const;

	//���Job�������й����б�ȡ����������������
	BOOL requestCancel();

public:
	//�����������һ��, ��������������Job�� if( Initialize ){ Run -> Finalize }
	virtual BOOL initialize(/*CBasePayService* pBasePayService, void* param = NULL*/);

protected:

	// �����Run��ͨ����Ҫѭ�� ���� GetJobWaitType �������
	virtual BOOL _run() = 0;

	// �����new�����ģ�ͨ����Ҫ�� Finalize �е��� delete this (������������������ڹ�������)
	virtual VOID _finalize();

	// �����������δ���е�Job(ֱ��ȡ�����̳߳�ֹͣ), ��������ڴ����Դ, �� delete this ��
	virtual VOID _onCancelJob() = 0;

protected:

	virtual void _notifyProgress(LONG64 nCurPos, LONG64 nTotalSize);
	virtual void _notifyIndex();

	virtual void _notifyError(DWORD dwError, LPCTSTR pszDescription);

	virtual void _notifyCancel();

	// ͨ���ú�������ȡ�̳߳ص�״̬(Stop/Pause)���Լ�Job�Լ���Stop, �÷�ͬ CFThread:GetThreadWaitType:
	// �����֧����ͣ�������� INFINITE���粻��֧����ͣ(�����紫��)��������� 0
	RThreadWaitType _getJobWaitType(DWORD dwMilliseconds = INFINITE) const;


	HANDLE  get_stop_event_job()
	{
		return stop_job_event_;
	}

	// ��紫��
	HANDLE get_cancel_job_event();

	void signal_sync_cancel_job_event();


private:

	//����Ϊ˽�еı����ͷ�������ʹ������Ҳ��Ҫֱ�Ӹ��ģ���Pool���ý��п���
	int		job_priority_;
	HANDLE	stop_job_event_;					//ֹͣJob���¼����ñ�������Pool�������ͷ�(TODO:Pool�л���?)

	int		job_index_;
	RThreadPool* thread_pool_;


	HANDLE sync_cancel_job_event_;
};

#endif

