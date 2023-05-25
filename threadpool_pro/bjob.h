#ifndef BJOB_H
#define BJOB_H
#include "threadpool/rbasejob.h"


class BJob : public RBaseJob
{
public:
    BJob();
    virtual ~BJob();

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

#endif // BJOB_H
