cmake_minimum_required(VERSION 2.6)

project(SectBattleServer)

set(SERVER "sect_battle_svrd")
set(CMAKE_CXX_FLAGS "-std=c++11 -Wall -Wextra -Werror")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter -Wno-unused-variable")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-private-field -Wno-unused-const-variable")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG")
#set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")

include_directories(src)
include_directories(alpha)

find_library(PROTOBUF "protobuf")
find_library(GFLAGS "gflags")
find_library(PTHREAD "pthread")

file(GLOB SERVER_SRCS src/*.cc)

add_subdirectory(alpha)

set(PROTOFILES "protofiles")
set(SOURCE_FILE_DIR "${PROJECT_SOURCE_DIR}/src")
add_custom_command(OUTPUT ${SOURCE_FILE_DIR}/sect_battle_protocol.pb.h ${SOURCE_FILE_DIR}/sect_battle_protocol.pb.cc COMMAND protoc -I=${SOURCE_FILE_DIR} --cpp_out=${SOURCE_FILE_DIR} ${SOURCE_FILE_DIR}/sect_battle_protocol.proto DEPENDS ${SOURCE_FILE_DIR}/sect_battle_protocol.proto)
add_custom_target(${PROTOFILES} ALL DEPENDS ${SOURCE_FILE_DIR}/sect_battle_protocol.pb.h)
add_executable(${SERVER} ${SERVER_SRCS})
target_link_libraries(${SERVER} "alpha" ${PROTOBUF} ${GFLAGS} ${PTHREAD})
add_dependencies(${SERVER} ${PROTOFILES})
