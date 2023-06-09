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

// 在这个Run中通常需要循环 调用 GetJobWaitType 方法检测
BOOL BJob::_run()
{
    RSLOG_DEBUG << "job " << name.c_str() <<" running...";
    Sleep(100);
    return TRUE;
}

// 如果是new出来的，通常需要在 Finalize 中调用 delete this (除非又有另外的生存期管理容器)
void BJob::_finalize()
{
    RSLOG_DEBUG << "job " << name.c_str() <<" run end, finished!";
}

// 这个函数用于未运行的Job(直接取消或线程池停止), 用于清除内存等资源, 如 delete this 等
void BJob::_onCancelJob()
{
     RSLOG_DEBUG << "job " << name.c_str() <<" canceled!";
}
