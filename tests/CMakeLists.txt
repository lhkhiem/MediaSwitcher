find_package(Qt6 COMPONENTS Core REQUIRED)

add_executable(WorkspaceStateStoreTest
    WorkspaceStateStoreTest.cpp
    ../src/app/WorkspaceStateStore.cpp
    ../src/app/WorkspaceStateStore.h
)

target_include_directories(WorkspaceStateStoreTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(WorkspaceStateStoreTest PRIVATE Qt6::Core)
add_custom_command(TARGET WorkspaceStateStoreTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Core>"
        "$<TARGET_FILE_DIR:WorkspaceStateStoreTest>"
)

add_test(NAME WorkspaceStateStoreRoundTrip COMMAND WorkspaceStateStoreTest)
