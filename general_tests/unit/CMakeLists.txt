# Unit Tests CMakeLists.txt
# Tests individual modules/components in isolation

# Quick performance module test (header-only, no external dependencies)
add_executable(quick_test
    quick_test.cpp
)
target_link_libraries(quick_test ${COMMON_LIBRARIES})

# Simple performance validation test (tests performance optimization modules)
add_executable(simple_performance_test  
    simple_performance_test.cpp
)
target_link_libraries(simple_performance_test ${COMMON_LIBRARIES})

# Core utilities unit tests
add_executable(core_utils_test
    core_utils_test.cpp
    ${CORE_SOURCES}
)
target_link_libraries(core_utils_test ${COMMON_LIBRARIES})

# Transport layer unit tests
add_executable(transport_unit_test
    transport_unit_test.cpp
    ${CORE_SOURCES}
    ${TRANSPORT_SOURCES}
    ${APP_SOURCES}
)
target_link_libraries(transport_unit_test ${COMMON_LIBRARIES})

# Custom target to run all unit tests
add_custom_target(unit_tests
    COMMAND quick_test
    COMMAND simple_performance_test
    COMMAND core_utils_test  
    COMMAND transport_unit_test
    DEPENDS quick_test simple_performance_test core_utils_test transport_unit_test
    COMMENT "Running unit tests"
)