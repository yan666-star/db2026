set(SOURCES
    prepared_dictionary.cpp
    socket_io.cpp
    wire_codec.cpp
    wire_messages.cpp
)

add_library(network STATIC ${SOURCES})
target_include_directories(network PUBLIC ${CMAKE_SOURCE_DIR}/src)
