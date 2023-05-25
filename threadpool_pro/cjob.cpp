#include "cjob.h"
#include "rslogging.h"



CJob::CJob()
    : name("C")
{
    RSLOG_DEBUG << "construct class object " << name.c_str();
}

CJob::~CJob()
{
    RSLOG_DEBUG << "desconstruct class object " << name.c_str();
}

BOOL CJob::_initialize()
{
    RSLOG_DEBUG << "job " << name.c_str() <<" initisalize";
    return TRUE;
}

// �����Run��ͨ����Ҫѭ�� ���� GetJobWaitType �������
BOOL CJob::_run()
{
    RSLOG_DEBUG << "job " << name.c_str() <<" running...";
    Sleep(100);
    return TRUE;
}

// �����new�����ģ�ͨ����Ҫ�� Finalize �е��� delete this (������������������ڹ�������)
void CJob::_finalize()
{
    RSLOG_DEBUG << "job " << name.c_str() <<" run end, finished!";
}

// �����������δ���е�Job(ֱ��ȡ�����̳߳�ֹͣ), ��������ڴ����Դ, �� delete this ��
void CJob::_onCancelJob()
{
     RSLOG_DEBUG << "job " << name.c_str() <<" canceled!";
}
