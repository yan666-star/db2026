set(SOURCES
    connection_session.cpp
    prepared_dictionary.cpp
    request_dispatcher.cpp
    socket_io.cpp
    wire_codec.cpp
    wire_messages.cpp
)

add_library(network STATIC ${SOURCES})
target_include_directories(network PUBLIC ${CMAKE_SOURCE_DIR}/src)
