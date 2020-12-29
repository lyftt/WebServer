cmake_minimum_required(VERSION 2.8.12)

project(WebServer)

set(CMAKE_CXX_STANDARD 11)
add_definitions(-std=c++11)
add_definitions(-g)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/hiredis)
#include_directories(${CMAKE_SOURCE_DIR}/include/mysql)
link_directories(${CMAKE_SOURCE_DIR}/lib/mysql)
link_directories(${CMAKE_SOURCE_DIR}/lib/hiredis)

AUX_SOURCE_DIRECTORY(src SRC_LIST)

add_executable(WebServer ${SRC_LIST})
target_link_libraries(WebServer mysqlclient pthread hiredis)
