#include "bjob.h"
#include "rslogging.h"


BJob::BJob()
    : name("B")
{
    RSLOG_DEBUG << "construct class object " << name.c_str();
}

BJob::~BJob()
{
    RSLOG_DEBUG << "desconstruct class object " << name.c_str();
}

BOOL BJob::_initialize()
{
    RSLOG_DEBUG << "job " << name.c_str() <<" initisalize";
    return TRUE;
}

// �����Run��ͨ����Ҫѭ�� ���� GetJobWaitType �������
BOOL BJob::_run()
{
    RSLOG_DEBUG << "job " << name.c_str() <<" running...";
    Sleep(100);
    return TRUE;
}

// �����new�����ģ�ͨ����Ҫ�� Finalize �е��� delete this (������������������ڹ�������)
void BJob::_finalize()
{
    RSLOG_DEBUG << "job " << name.c_str() <<" run end, finished!";
}

// �����������δ���е�Job(ֱ��ȡ�����̳߳�ֹͣ), ��������ڴ����Դ, �� delete this ��
void BJob::_onCancelJob()
{
     RSLOG_DEBUG << "job " << name.c_str() <<" canceled!";
}
