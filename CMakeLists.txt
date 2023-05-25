# cmake最低版本
cmake_minimum_required(VERSION 3.14)
# 本项目名
project(windows-threadpool-pro)

# 设置C++17标准，需要满足标准，不用C++拓展语法
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)

set(CMAKE_BUILD_TYPE "Release")

# 进入logger文件夹下面的
add_subdirectory(logger)

# threadpool_pro
add_subdirectory(threadpool_pro)


