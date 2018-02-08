cmake_minimum_required(VERSION 3.2)
project(tinyhttpd)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

add_executable(simpleclient simpleclient.c)
add_executable(tinyhttpd httpd.c)
