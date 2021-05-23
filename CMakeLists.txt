cmake_minimum_required(VERSION 3.17)
project(SiK2)

set(CMAKE_CXX_STANDARD 17)


add_executable(serwer serwer.cpp crc.cpp common.h common.cpp)
add_executable(client client.cpp crc.cpp common.h common.cpp)