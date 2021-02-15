cmake_minimum_required(VERSION 3.10)

# set the project name
project(raft)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# for use with clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# generate protobuf cpp files
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS src/RaftRPC.proto)

set(SERVER_SRCS
  src/Server.cpp
  src/Messenger.cpp
  src/Networker.cpp
  src/util.cpp
  ${PROTO_SRCS})

set(SERVER_HDRS
  src/Server.h
  src/Messenger.h
  src/Networker.h
  src/util.h
  ${PROTO_HDRS})

set(CLIENT_SRCS
  src/Client.cpp
  src/Messenger.cpp
  src/Networker.cpp
  src/util.cpp
  ${PROTO_SRCS})

set(CLIENT_HDRS
  src/Client.h
  src/Messenger.h
  src/Networker.h
  src/util.h
  ${PROTO_HDRS})

set(TEST_SRCS
  src/Networker.cpp
  src/Messenger.cpp
  ${PROTO_SRCS})

set(TEST_HDRS
  src/Networker.h
  src/Messenger.h
  ${PROTO_HDRS})

# add the executables
add_executable(raft_server src/server-main.cpp ${SERVER_SRCS} ${SERVER_HDRS})
target_link_libraries(raft_server ${Protobuf_LIBRARIES})

add_executable(raft_client src/client-main.cpp ${CLIENT_SRCS} ${CLIENT_HDRS})
target_link_libraries(raft_client ${Protobuf_LIBRARIES})

add_executable(test_driver src/TestDriver.cpp ${TEST_SRCS} ${TEST_HDRS})
target_link_libraries(test_driver ${Protobuf_LIBRARIES})

# so we can include generated protobuf headers
target_include_directories(raft_server PUBLIC build)
target_include_directories(raft_client PUBLIC build)
target_include_directories(test_driver PUBLIC build)
