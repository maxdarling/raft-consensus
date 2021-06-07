cmake_minimum_required(VERSION 3.10)

# set the project name
project(raft)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# for use with clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# set compile flags 
add_compile_options(-Wall -Werror)

# generate protobuf cpp files
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS src/RaftRPC.proto src/RaftPersistentState.proto)

set(SERVER_SRCS
  src/Server.cpp
  src/Messenger.cpp
  src/TimedCallback.cpp
  src/util.cpp
  src/loguru/loguru.cpp
  ${PROTO_SRCS})

set(SERVER_HDRS
  src/Server.h
  src/Messenger.h
  src/TimedCallback.h
  src/Log.h
  src/PersistentStorage.h
  src/util.h
  src/BlockingQueue.h
  src/loguru/loguru.hpp
  ${PROTO_HDRS})

set(CLIENT_SRCS
  src/Client.cpp
  src/Messenger.cpp
  src/util.cpp
  src/loguru/loguru.cpp
  ${PROTO_SRCS})

set(CLIENT_HDRS
  src/Client.h
  src/Messenger.h
  src/util.h
  src/BlockingQueue.h
  src/loguru/loguru.hpp
  ${PROTO_HDRS})

set(TEST_SRCS
  src/Messenger.cpp
  src/util.cpp
  src/loguru/loguru.cpp
  ${PROTO_SRCS})

set(TEST_HDRS
  src/Messenger.h
  src/BlockingQueue.h
  src/util.h
  src/loguru/loguru.hpp
  ${PROTO_HDRS})

# add the executables
add_executable(raft_server src/server-main.cpp ${SERVER_SRCS} ${SERVER_HDRS})
target_link_libraries(raft_server ${Protobuf_LIBRARIES})

add_executable(raft_client src/client-main.cpp ${CLIENT_SRCS} ${CLIENT_HDRS})
target_link_libraries(raft_client ${Protobuf_LIBRARIES})

add_executable(test_driver src/TestDriver.cpp ${TEST_SRCS} ${TEST_HDRS})
target_link_libraries(test_driver ${Protobuf_LIBRARIES})

#add_executable(log_tests src/log-tests.cpp src/Log.h)

# so we can include generated protobuf headers
target_include_directories(raft_server PUBLIC build)
target_include_directories(raft_client PUBLIC build)
target_include_directories(test_driver PUBLIC build)
