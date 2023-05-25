#include <Windows.h>
#include <functional>
#include <assert.h>

#ifndef _THREAD_POOL_CONST_H__
#define _THREAD_POOL_CONST_H__


#if defined ATLASSERT
# define TPASSERT          ATLASSERT
#elif defined ASSERT
# define TPASSERT          ASSERT
#else
# define TPASSERT			assert 
#endif

//禁止拷贝构造和赋值操作符
#define DISABLE_COPY_AND_ASSIGNMENT(className)  \
private:\
	className(const className& ref);\
	className& operator = (const className& ref)

// _countof定义
#ifndef _countof
#  define _countof(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

#define API_VERIFY(x)   \
	bRet = (x); \
	TPASSERT(TRUE == bRet);

#define COM_VERIFY(x)   \
	hr = (x); \
	TPASSERT(SUCCEEDED(hr));

#define DX_VERIFY(x)   \
	hr = (x); \
	TPASSERT(SUCCEEDED(hr));

#ifndef SAFE_FREE_BSTR
#  define SAFE_FREE_BSTR(s) if(NULL != (s) ){ ::SysFreeString((s)); (s) = NULL; }
#endif

#ifndef SAFE_RELEASE
#  define SAFE_RELEASE(p)  if( NULL != (p) ){ (p)->Release(); (p) = NULL; }
#endif 

#ifndef SAFE_DELETE
#  define SAFE_DELETE(p)	if(NULL != (p) ) { delete p; p = NULL; }
#endif

#  define SAFE_CLOSE_HANDLE(h,v) if((v) != (h)) { ::CloseHandle((h)); (h) = (v); bRet = bRet; }
#  define SAFE_DELETE_ARRAY(p) if( NULL != (p) ){ delete [] (p); (p) = NULL; }


#define CHECK_POINTER_RETURN_VALUE_IF_FAIL(p,r)    \
	if(NULL == p)\
	{\
	TPASSERT(NULL != p);\
	return r;\
	}

#define COMPARE_MEM_LESS(f, o) \
	if( f < o.f ) { return true; }\
		else if( f > o.f ) { return false; }

// 带锁的泛型类
template<typename T = RLockObject>
class CFAutoLock
{
public:
	explicit CFAutoLock<T>(T* pLockObj)
	{
		m_pLockObj = pLockObj;
		m_pLockObj->Lock(INFINITE);
	}
	~CFAutoLock()
	{
		m_pLockObj->UnLock();
	}
private:
	T*   m_pLockObj;
};

//////////////////////////////////////////////////////////////////////////
// 比较大小
template <class Arg1, class Arg2, class Result> struct binary_function;

template <class Arg1, class Arg2, class Result>
struct binary_function {
	typedef Arg1 first_argument_type;
	typedef Arg2 second_argument_type;
	typedef Result result_type;
};

template <typename T> 
struct UnreferenceLess : public binary_function<T, T, bool>
{
	bool operator()(const T& _Left, const T& _Right) const
	{
		return (*_Left < *_Right);
	}
};

#endif