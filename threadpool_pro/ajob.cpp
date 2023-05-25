#include "ajob.h"
#include "rslogging.h"

AJob::AJob()
    : name("A")
{
    RSLOG_DEBUG << "construct class object " << name.c_str();
}

AJob::~AJob()
{
    RSLOG_DEBUG << "desconstruct class object " << name.c_str();
}

BOOL AJob::_initialize()
{
    RSLOG_DEBUG << "job " << name.c_str() <<" initisalize";
    return TRUE;
}

// �����Run��ͨ����Ҫѭ�� ���� GetJobWaitType �������
BOOL AJob::_run()
{
    RSLOG_DEBUG << "job " << name.c_str() <<" running...";
    Sleep(100);
    return TRUE;
}

// �����new�����ģ�ͨ����Ҫ�� Finalize �е��� delete this (������������������ڹ�������)
void AJob::_finalize()
{
    RSLOG_DEBUG << "job " << name.c_str() <<" run end, finished!";
}

// �����������δ���е�Job(ֱ��ȡ�����̳߳�ֹͣ), ��������ڴ����Դ, �� delete this ��
void AJob::_onCancelJob()
{
     RSLOG_DEBUG << "job " << name.c_str() <<" canceled!";
}
