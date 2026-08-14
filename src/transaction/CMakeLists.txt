set(SOURCES concurrency/lock_manager.cpp index_version_store.cpp
    storage_commit_executor.cpp transaction_manager.cpp
    transaction_write_batch.cpp watermark.cpp)
add_library(transaction STATIC ${SOURCES})
target_link_libraries(transaction system recovery pthread)
