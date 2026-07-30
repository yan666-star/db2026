set(SOURCES
    socket_io.cpp
    wire_codec.cpp
)

add_library(network STATIC ${SOURCES})
target_include_directories(network PUBLIC ${CMAKE_SOURCE_DIR}/src)
