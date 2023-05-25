#ifndef _R_THREAD_POOL_CALL_BACK_H__
#define _R_THREAD_POOL_CALL_BACK_H__

#include "threadpool/rbasejob.h"

// �ص�����
class RThreadPoolCallBack
{
public:

	RThreadPoolCallBack();

	virtual ~RThreadPoolCallBack();

	//��Job���������Ժ󣬻��� Pool ���� Begin �� End ��������

	virtual void onJobBegin(int nJobIndex, RBaseJob* pJob );

	virtual void onJobEnd(int nJobIndex, RBaseJob* pJob);

	//�����δ��������״̬�ͱ�ȡ����Job������Pool�����������
	virtual void onJobCancel(int nJobIndex, RBaseJob* pJob);


	//Progress �� Error �� JobBase �����༤��

	virtual void onJobProgress(int nJobIndex , RBaseJob* pJob, LONG64 nCurPos, LONG64 nTotalSize);


	virtual void onJobError(int nJobIndex , RBaseJob* pJob, DWORD dwError, LPCTSTR pszDescription);


};

#endif

