include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(analyze)
add_subdirectory(record)
add_subdirectory(index)
add_subdirectory(system)
add_subdirectory(execution)
add_subdirectory(parser)
add_subdirectory(optimizer)
add_subdirectory(storage)
add_subdirectory(common)
add_subdirectory(network)
add_subdirectory(replacer)
add_subdirectory(transaction)
add_subdirectory(recovery)
add_subdirectory(test)


target_link_libraries(parser execution pthread)

add_executable(rmdb rmdb.cpp)
target_link_libraries(rmdb parser execution recovery readline pthread planner analyze)

# unit_test
add_executable(unit_test unit_test.cpp)
target_link_libraries(unit_test storage lru_replacer record gtest_main)  # add gtest

add_executable(test_wire_codec test/wire_codec_test.cpp)
target_link_libraries(test_wire_codec network)
add_test(NAME test_wire_codec COMMAND test_wire_codec)

add_executable(test_socket_io test/socket_io_test.cpp)
target_link_libraries(test_socket_io network)
add_test(NAME test_socket_io COMMAND test_socket_io)

add_executable(test_execution_result test/execution_result_test.cpp)
target_link_libraries(test_execution_result network)
add_test(NAME test_execution_result COMMAND test_execution_result)

add_executable(test_prepared_dictionary test/prepared_dictionary_test.cpp)
target_link_libraries(test_prepared_dictionary network)
add_test(NAME test_prepared_dictionary COMMAND test_prepared_dictionary)

add_executable(test_wire_messages test/wire_messages_test.cpp)
target_link_libraries(test_wire_messages network)
add_test(NAME test_wire_messages COMMAND test_wire_messages)
