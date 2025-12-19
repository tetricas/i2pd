# Integration Tests CMakeLists.txt
# Tests interaction between multiple components

# Full streaming integration test (existing embedded_stream_itest)
add_executable(streaming_integration_test
    ${TESTS_CLIENT_APP_DIR}/embedded_stream_itest.cpp
    ${CORE_SOURCES}
    ${TRANSPORT_SOURCES}
    ${APP_SOURCES}
)
target_link_libraries(streaming_integration_test ${COMMON_LIBRARIES})

# File transfer integration test (existing chunked_file_test)  
add_executable(file_transfer_integration_test
    ${TESTS_CLIENT_APP_DIR}/chunked_file_test.cpp
    ${CORE_SOURCES}
    ${TRANSPORT_SOURCES}
    ${FILETRANSFER_SOURCES}
    ${APP_SOURCES}
)
target_link_libraries(file_transfer_integration_test ${COMMON_LIBRARIES})

# End-to-end performance optimization integration test
add_executable(e2e_performance_test
    e2e_performance_test.cpp
    ${CORE_SOURCES}
    ${TRANSPORT_SOURCES}
    ${FILETRANSFER_SOURCES}
    ${APP_SOURCES}
)
target_link_libraries(e2e_performance_test ${COMMON_LIBRARIES})

# Custom target to run all integration tests
add_custom_target(integration_tests
    COMMAND streaming_integration_test --help
    COMMAND file_transfer_integration_test --help
    COMMAND e2e_performance_test
    DEPENDS streaming_integration_test file_transfer_integration_test e2e_performance_test
    COMMENT "Running integration tests"
)