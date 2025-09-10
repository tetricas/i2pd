# Performance Tests CMakeLists.txt  
# Benchmarks and performance validation tests

# Comprehensive performance test suite (Session 10 implementation)
add_executable(comprehensive_performance_test
    performance_test.cpp
    ${CORE_SOURCES}
    ${TRANSPORT_SOURCES}
    ${FILETRANSFER_SOURCES}
    ${APP_SOURCES}
)
target_link_libraries(comprehensive_performance_test ${COMMON_LIBRARIES})

# Streaming protocol benchmark
add_executable(streaming_benchmark
    streaming_benchmark.cpp
    ${CORE_SOURCES}
    ${TRANSPORT_SOURCES}
    ${APP_SOURCES}
)
target_link_libraries(streaming_benchmark ${COMMON_LIBRARIES})

# File transfer throughput benchmark
add_executable(file_transfer_benchmark
    file_transfer_benchmark.cpp
    ${CORE_SOURCES}
    ${TRANSPORT_SOURCES}
    ${FILETRANSFER_SOURCES}
    ${APP_SOURCES}
)
target_link_libraries(file_transfer_benchmark ${COMMON_LIBRARIES})

# Compression performance benchmark
add_executable(compression_benchmark
    compression_benchmark.cpp
)
target_link_libraries(compression_benchmark ${COMMON_LIBRARIES})

# Custom target to run all performance tests
add_custom_target(performance_tests
    COMMAND comprehensive_performance_test
    COMMAND streaming_benchmark
    COMMAND file_transfer_benchmark
    COMMAND compression_benchmark
    DEPENDS comprehensive_performance_test streaming_benchmark file_transfer_benchmark compression_benchmark
    COMMENT "Running performance benchmarks"
)