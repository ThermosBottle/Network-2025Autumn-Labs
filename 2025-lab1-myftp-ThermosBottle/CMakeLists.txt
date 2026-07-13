cmake_minimum_required(VERSION 3.22)

project(2025-lab1-myFTP)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# 添加 include 目录
include_directories(${CMAKE_SOURCE_DIR}/include)


add_executable(ftp_server ftp_server.cpp)
add_executable(ftp_client ftp_client.cpp)