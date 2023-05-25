#ifndef CJOB_H
#define CJOB_H
#include "threadpool/rbasejob.h"


class CJob : public RBaseJob
{
public:
    CJob();
    virtual ~CJob();

protected:
    virtual BOOL _initialize();

    // �����Run��ͨ����Ҫѭ�� ���� GetJobWaitType �������
    virtual BOOL _run();

    // �����new�����ģ�ͨ����Ҫ�� Finalize �е��� delete this (������������������ڹ�������)
    virtual void _finalize();

    // �����������δ���е�Job(ֱ��ȡ�����̳߳�ֹͣ), ��������ڴ����Դ, �� delete this ��
    virtual void _onCancelJob();


private:
    std::string name;
};

#endif // CJOB_H
