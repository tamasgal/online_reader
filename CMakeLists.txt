cmake_minimum_required(VERSION 2.8)
project(OnlineReader CXX)

link_directories(${OnlineReader_BINARY_DIR}/)

add_library(choo STATIC controlhost/choo.cxx controlhost/tcpnet.cxx controlhost/byaddr.cxx)
set_target_properties(choo PROPERTIES COMPILE_FLAGS "-Wno-unused-variable")
#set (CMAKE_CXX_FLAGS "-std=c++0x -stdlib=libc++ -g3 -Wall -O0 -lchoo")

add_executable(OnlineReader OnlineReader.cxx)
target_link_libraries (OnlineReader LINK_PUBLIC choo )
#target_include_directories(OnlineReader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
