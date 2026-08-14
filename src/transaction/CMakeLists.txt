set(SOURCES concurrency/lock_manager.cpp index_version_store.cpp
    transaction_manager.cpp watermark.cpp)
add_library(transaction STATIC ${SOURCES})
target_link_libraries(transaction system recovery pthread)
