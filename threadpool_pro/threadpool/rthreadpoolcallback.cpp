#include "threadpool/rthreadpoolcallback.h"
#include "rslogger_declare.h"
#include "rslog.h"
#include "rslogging.h"


RThreadPoolCallBack::RThreadPoolCallBack()
{
}


RThreadPoolCallBack::~RThreadPoolCallBack()
{
}


void RThreadPoolCallBack::onJobBegin(int nJobIndex, RBaseJob* pJob )
{
	RSLOG_DEBUG << "onJobBegin";
} 

void RThreadPoolCallBack::onJobEnd(int nJobIndex, RBaseJob* pJob)
{
	RSLOG_DEBUG << "onJobEnd";
}

//如果尚未到达运行状态就被取消的Job，会由Pool调用这个函数
void RThreadPoolCallBack::onJobCancel(int nJobIndex, RBaseJob* pJob)
{
	RSLOG_DEBUG << "onJobCancel, " << nJobIndex;
}

//Progress 和 Error 由 JobBase 的子类激发
void RThreadPoolCallBack::onJobProgress(int nJobIndex , RBaseJob* pJob, LONG64 nCurPos, LONG64 nTotalSize)
{
	RSLOG_DEBUG << "onJobProgress, " << nJobIndex;
}

void RThreadPoolCallBack::onJobError(int nJobIndex , RBaseJob* pJob, DWORD dwError, LPCTSTR pszDescription)
{
	RSLOG_DEBUG << "onJobError, " << nJobIndex;
}