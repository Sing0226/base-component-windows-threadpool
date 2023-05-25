/////////////////////////////////////////////////////////////////////////////////
/// Copyright (C), 2022, 
/// Rayshape Corporation. All rights reserved.
/// @file    rbasebob.h
/// @author  Simone
/// @date    2023/03/28
/// @brief   
///
/// @history v0.01 2015/11/07  单元创建
/////////////////////////////////////////////////////////////////////////////////
///

#ifndef _R_BASE_JOB_H__
#define _R_BASE_JOB_H__

#include <Windows.h>
#include "threadpool/rthreadpoolbase.h"

//  前向声明，
//! 具有以下特点：
//  1.能自动根据任务和线程的多少在 最小/最大 线程个数之间调整
//  2.能方便的对单个任务进行取消，如任务尚未运行则由框架代码处理，如任务已经运行，则需要 JobBase 的子类根据 GetJobWaitType 的返回值进行处理
//  3.能对整个线程池进行 暂停、继续、停止 处理 -- 需要 JobBase 的子类根据 GetJobWaitType 的返回值进行处理
//  4.支持回调方式的反馈通知( Progress/Error 等)
//  5.使用的是微软的基本API，能支持WinXP、Vista、Win7等各种操作系统
class RThreadPool;


//////////////////////////////////////////////////////////////////////////
// 定义Job基类,后期可以用模板作为参数也行，这个就交给张方去实现吧。

class RBaseJob
{
	friend class RThreadPool;   //允许Threadpool设置 m_pThreadPool/m_nJobIndex 的值

	template <typename T> friend struct UnreferenceLess;

public:

	RBaseJob(int job_priority = 0);
	virtual ~RBaseJob();

	//! 比较Job的优先级大小，用于确定在 Waiting 容器中的队列， 排序依据为 Priority -> Index
	bool operator < (const RBaseJob& other) const;

	int getJobIndex() const;

	//如果Job正在运行过程中被取消，会调用这个方法
	BOOL requestCancel();

public:
	//这个三个函数一组, 用于运行起来的Job： if( Initialize ){ Run -> Finalize }
	virtual BOOL initialize(/*CBasePayService* pBasePayService, void* param = NULL*/);

protected:

	// 在这个Run中通常需要循环 调用 GetJobWaitType 方法检测
	virtual BOOL _run() = 0;

	// 如果是new出来的，通常需要在 Finalize 中调用 delete this (除非又有另外的生存期管理容器)
	virtual VOID _finalize();

	// 这个函数用于未运行的Job(直接取消或线程池停止), 用于清除内存等资源, 如 delete this 等
	virtual VOID _onCancelJob() = 0;

protected:

	virtual void _notifyProgress(LONG64 nCurPos, LONG64 nTotalSize);
	virtual void _notifyIndex();

	virtual void _notifyError(DWORD dwError, LPCTSTR pszDescription);

	virtual void _notifyCancel();

	// 通过该函数，获取线程池的状态(Stop/Pause)，以及Job自己的Stop, 用法同 CFThread:GetThreadWaitType:
	// 如果想支持暂停，参数是 INFINITE；如不想支持暂停(如网络传输)，则参数传 0
	RThreadWaitType _getJobWaitType(DWORD dwMilliseconds = INFINITE) const;


	HANDLE  get_stop_event_job()
	{
		return stop_job_event_;
	}

	// 外界传入
	HANDLE get_cancel_job_event();

	void signal_sync_cancel_job_event();


private:

	//设置为私有的变量和方法，即使是子类也不要直接更改，由Pool调用进行控制
	int		job_priority_;
	HANDLE	stop_job_event_;					//停止Job的事件，该变量将由Pool创建和释放(TODO:Pool中缓存?)

	int		job_index_;
	RThreadPool* thread_pool_;


	HANDLE sync_cancel_job_event_;
};

#endif

