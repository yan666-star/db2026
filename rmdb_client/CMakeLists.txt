cmake_minimum_required(VERSION 3.16)
project(rmdb_client)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

add_executable(
        ${PROJECT_NAME}
        main.cpp
        ../src/network/socket_io.cpp
        ../src/network/wire_codec.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ../src)

target_link_libraries(rmdb_client
        pthread readline 
)
