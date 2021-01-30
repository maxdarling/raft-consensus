cmake_minimum_required(VERSION 3.10)

# set the project name
project(raft)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_BUILD_TYPE Debug)

# generate protobuf cpp files
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS src/RaftRPC.proto)

set(SOURCE_FILES 
  src/Networker.cpp
  src/Messenger.cpp
  ${PROTO_SRCS})

set(HEADER_FILES
  src/Networker.h
  src/Messenger.cpp
  ${PROTO_HDRS})

set(SERVER_SRCS
  src/Server.cpp
  src/Messenger.cpp
  src/Networker.cpp
  ${PROTO_SRCS})

set(SERVER_HDRS
  src/Server.h
  src/Messenger.h
  src/Networker.h
  ${PROTO_HDRS})

# add the executables
add_executable(${PROJECT_NAME} src/main.cpp ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(${PROJECT_NAME} ${Protobuf_LIBRARIES})

add_executable(run_server src/server-main.cpp ${SERVER_SRCS} ${SERVER_HDRS})
target_link_libraries(run_server ${Protobuf_LIBRARIES})

# so we can include generated protobuf headers
target_include_directories(${PROJECT_NAME} PUBLIC build)
target_include_directories(run_server PUBLIC build)
