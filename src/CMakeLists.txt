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
target_link_libraries(rmdb -Wl,--start-group parser execution -Wl,--end-group recovery pthread planner analyze network)

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

add_executable(test_request_dispatcher test/request_dispatcher_test.cpp)
target_link_libraries(test_request_dispatcher network)
add_test(NAME test_request_dispatcher COMMAND test_request_dispatcher)

add_executable(test_connection_session test/connection_session_test.cpp)
target_link_libraries(test_connection_session network)
add_test(NAME test_connection_session COMMAND test_connection_session)

add_executable(test_parameter_binding test/parameter_binding_test.cpp)
target_link_libraries(test_parameter_binding network)
add_test(NAME test_parameter_binding COMMAND test_parameter_binding)

add_executable(test_ssi_source_contract test/ssi_source_contract_test.cpp)
add_test(
    NAME test_ssi_source_contract
    COMMAND test_ssi_source_contract ${CMAKE_SOURCE_DIR}
)

add_executable(
    test_log_manager_durability
    test/log_manager_durability_test.cpp
)
target_link_libraries(test_log_manager_durability recovery storage pthread)
add_test(NAME test_log_manager_durability COMMAND test_log_manager_durability)

add_executable(test_group_commit test/group_commit_test.cpp)
target_link_libraries(test_group_commit recovery storage pthread)
add_test(NAME test_group_commit COMMAND test_group_commit)

add_executable(test_wal_page_lsn test/wal_page_lsn_test.cpp)
target_link_libraries(test_wal_page_lsn recovery storage pthread)
add_test(NAME test_wal_page_lsn COMMAND test_wal_page_lsn)

add_executable(test_page_guard test/page_guard_test.cpp)
target_link_libraries(test_page_guard recovery storage pthread)
add_test(NAME test_page_guard COMMAND test_page_guard)

add_executable(test_buffer_pool_concurrency test/buffer_pool_concurrency_test.cpp)
target_link_libraries(test_buffer_pool_concurrency recovery storage pthread)
add_test(NAME test_buffer_pool_concurrency COMMAND test_buffer_pool_concurrency)

add_executable(test_server_memory_budget test/server_memory_budget_test.cpp)
add_test(NAME test_server_memory_budget COMMAND test_server_memory_budget)

add_executable(test_aggregate_distinct test/aggregate_distinct_test.cpp)
target_link_libraries(test_aggregate_distinct execution record system pthread)
add_test(NAME test_aggregate_distinct
         COMMAND test_aggregate_distinct ${CMAKE_SOURCE_DIR})

add_executable(test_batch_visibility test/batch_visibility_test.cpp)
target_link_libraries(test_batch_visibility transaction record system recovery pthread)
add_test(NAME test_batch_visibility COMMAND test_batch_visibility)

add_executable(test_mvcc_commit_visibility test/mvcc_commit_visibility_test.cpp)
target_link_libraries(test_mvcc_commit_visibility transaction record system recovery pthread)
add_test(NAME test_mvcc_commit_visibility COMMAND test_mvcc_commit_visibility)

add_executable(test_mvcc_index_visibility test/mvcc_index_visibility_test.cpp)
target_link_libraries(test_mvcc_index_visibility transaction pthread)
add_test(NAME test_mvcc_index_visibility COMMAND test_mvcc_index_visibility)

add_executable(test_record_buffer_reuse test/record_buffer_reuse_test.cpp)
# record, system and transaction form a static-library dependency cycle.
# Keep them in one GNU ld rescan group so symbols first referenced by a later
# archive (notably SmManager -> RmScan) are resolved deterministically.
target_link_libraries(
    test_record_buffer_reuse
    -Wl,--start-group
    record system transaction recovery index storage
    -Wl,--end-group
    pthread)
add_test(NAME test_record_buffer_reuse COMMAND test_record_buffer_reuse)

add_executable(test_heap_page_concurrency test/heap_page_concurrency_test.cpp)
target_link_libraries(
    test_heap_page_concurrency
    -Wl,--start-group
    record system transaction recovery index storage
    -Wl,--end-group
    pthread)
add_test(NAME test_heap_page_concurrency COMMAND test_heap_page_concurrency)

add_executable(test_batch_insert test/batch_insert_test.cpp)
target_link_libraries(
    test_batch_insert
    -Wl,--start-group
    record system transaction recovery index storage
    -Wl,--end-group
    pthread)
add_test(NAME test_batch_insert COMMAND test_batch_insert)

add_executable(test_ix_concurrency test/ix_concurrency_test.cpp)
target_link_libraries(
    test_ix_concurrency
    -Wl,--start-group
    index storage system transaction recovery record
    -Wl,--end-group
    pthread)
add_test(NAME test_ix_concurrency COMMAND test_ix_concurrency)

add_executable(test_perf_counters test/perf_counters_test.cpp)
target_link_libraries(test_perf_counters pthread)
add_test(NAME test_perf_counters COMMAND test_perf_counters)

add_executable(test_projection_alias test/projection_alias_test.cpp)
add_test(NAME test_projection_alias COMMAND test_projection_alias)

add_executable(test_finals_workload_contract
    test/finals_workload_contract_test.cpp)
add_test(
    NAME test_finals_workload_contract
    COMMAND test_finals_workload_contract ${CMAKE_SOURCE_DIR}
)
