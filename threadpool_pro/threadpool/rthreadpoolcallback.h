#ifndef _R_THREAD_POOL_CALL_BACK_H__
#define _R_THREAD_POOL_CALL_BACK_H__

#include "threadpool/rbasejob.h"

// 回调函数
class RThreadPoolCallBack
{
public:

	RThreadPoolCallBack();

	virtual ~RThreadPoolCallBack();

	//当Job运行起来以后，会由 Pool 激发 Begin 和 End 两个函数

	virtual void onJobBegin(int nJobIndex, RBaseJob* pJob );

	virtual void onJobEnd(int nJobIndex, RBaseJob* pJob);

	//如果尚未到达运行状态就被取消的Job，会由Pool调用这个函数
	virtual void onJobCancel(int nJobIndex, RBaseJob* pJob);


	//Progress 和 Error 由 JobBase 的子类激发

	virtual void onJobProgress(int nJobIndex , RBaseJob* pJob, LONG64 nCurPos, LONG64 nTotalSize);


	virtual void onJobError(int nJobIndex , RBaseJob* pJob, DWORD dwError, LPCTSTR pszDescription);


};

#endif

