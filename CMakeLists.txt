cmake_minimum_required(VERSION 3.10)

# set the project name
project(raft)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# include Protocol Buffers
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})

set(SOURCE_FILES 
  Networker.cpp)

set(HEADER_FILES
  Networker.h)

# add the executable
add_executable(${PROJECT_NAME} main.cpp ${SOURCE_FILES} ${HEADER_FILES})
add_executable(run_server server-main.cpp)
