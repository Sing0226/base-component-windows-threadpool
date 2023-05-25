cmake_minimum_required(VERSION 3.14)

project(threadpool_win LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)


set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../logger)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/threadpool)

FILE(GLOB THREADPOOL_h
    "threadpool/rthreadpoolconst.h"
    "threadpool/rbasejob.h"
    "threadpool/rthreadpool.h"
    "threadpool/rthreadpoolbase.h"
    "threadpool/rthreadpoolcallback.h")

FILE(GLOB THREADPOOL_src
    "threadpool/rbasejob.cpp"
    "threadpool/rthreadpool.cpp"
    "threadpool/rthreadpoolbase.cpp"
    "threadpool/rthreadpoolcallback.cpp")

# source_group(Header Files/threadpool FILES ${THREADPOOL_h})
# source_group(Source Files/threadpool FILES ${THREADPOOL_src})

add_executable(threadpool_win
    threadpool/rthreadpoolconst.h
    threadpool/rbasejob.h
    threadpool/rbasejob.cpp
    threadpool/rthreadpool.h
    threadpool/rthreadpool.cpp
    threadpool/rthreadpoolbase.h
    threadpool/rthreadpoolbase.cpp
    threadpool/rthreadpoolcallback.h
    threadpool/rthreadpoolcallback.cpp	
    ajob.h
    ajob.cpp
    bjob.h
    bjob.cpp
    cjob.h
    cjob.cpp
	main.cpp
)
target_link_libraries(threadpool_win 
	logger 
)