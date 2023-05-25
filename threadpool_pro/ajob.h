#ifndef AJOB_H
#define AJOB_H
#include "threadpool/rbasejob.h"


class AJob : public RBaseJob
{
public:
    AJob();
    virtual ~AJob();

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

#endif // AJOB_H
