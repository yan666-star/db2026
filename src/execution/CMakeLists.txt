set(SOURCES
    execution_manager.cpp
    sql_execution_service.cpp
)
add_library(execution STATIC ${SOURCES})

target_link_libraries(execution system record transaction planner)
