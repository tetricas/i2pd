cmake_minimum_required(VERSION 3.12)
project(i2pd_tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Boost REQUIRED COMPONENTS system filesystem program_options)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)  
find_package(Threads REQUIRED)

# Set up test client paths
set(TESTS_CLIENT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tests_client)
set(TESTS_CLIENT_CORE_DIR ${TESTS_CLIENT_DIR}/core)
set(TESTS_CLIENT_TRANSPORT_DIR ${TESTS_CLIENT_DIR}/transport)
set(TESTS_CLIENT_FILETRANSFER_DIR ${TESTS_CLIENT_DIR}/filetransfer)
set(TESTS_CLIENT_APP_DIR ${TESTS_CLIENT_DIR}/app)

# Set library search paths
set(I2PD_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cmake-debug)
link_directories(${I2PD_LIB_DIR})

# Include directories  
include_directories(${TESTS_CLIENT_DIR} ${TESTS_CLIENT_CORE_DIR} ${TESTS_CLIENT_TRANSPORT_DIR} ${TESTS_CLIENT_FILETRANSFER_DIR} ${TESTS_CLIENT_APP_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../libi2pd_client)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../libi2pd)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../daemon)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../i18n)

# Core library sources (from tests_client directory)
set(CORE_SOURCES
    ${TESTS_CLIENT_CORE_DIR}/I2PdUtils.cpp
    ${TESTS_CLIENT_CORE_DIR}/ConnectionUtils.cpp
    ${TESTS_CLIENT_CORE_DIR}/ErrorHandling.cpp
    ${TESTS_CLIENT_CORE_DIR}/OperationContext.cpp
)

# Transport layer sources
set(TRANSPORT_SOURCES
    ${TESTS_CLIENT_TRANSPORT_DIR}/StreamFactory.cpp
    ${TESTS_CLIENT_TRANSPORT_DIR}/NormalStreamingImpl.cpp
    ${TESTS_CLIENT_TRANSPORT_DIR}/SimpleStreamingImpl.cpp
    ${TESTS_CLIENT_TRANSPORT_DIR}/SimpleStreamingProvider.cpp
    ${TESTS_CLIENT_TRANSPORT_DIR}/NormalStreamingProvider.cpp
    ${TESTS_CLIENT_TRANSPORT_DIR}/StreamingProviderRegistry.cpp
)

# File transfer sources  
set(FILETRANSFER_SOURCES
    ${TESTS_CLIENT_FILETRANSFER_DIR}/ChunkedFileClient.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/ChunkedFileServer.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/FileTransferFactory.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/SimpleMessagingFileClient.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/NormalStreamingFileClient.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/NormalStreamingFileServer.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/BinaryFileProtocol.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/TransferStrategies.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/TransferStrategyRegistry.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/TransferSubject.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/TransferCommands.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/TransferCommandQueue.cpp
    ${TESTS_CLIENT_FILETRANSFER_DIR}/TransferConfigBuilder.cpp
)

# Application sources
set(APP_SOURCES
    ${TESTS_CLIENT_APP_DIR}/CliParser.cpp
)

# Common libraries for all tests
set(COMMON_LIBRARIES
    ${I2PD_LIB_DIR}/libi2pdclient.a
    ${I2PD_LIB_DIR}/libi2pd.a
    ${I2PD_LIB_DIR}/libi2pdlang.a
    Boost::system Boost::filesystem Boost::program_options
    OpenSSL::SSL OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads
)

# Add subdirectories for different test types
add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(performance)

# Global test target to run all tests
add_custom_target(run_all_tests
    COMMAND ${CMAKE_COMMAND} --build . --target unit_tests
    COMMAND ${CMAKE_COMMAND} --build . --target integration_tests  
    COMMAND ${CMAKE_COMMAND} --build . --target performance_tests
    COMMENT "Running all test suites"
)

# Clean build artifacts target
add_custom_target(clean_tests
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/build/debug
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/build/release
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/*.log
    COMMENT "Cleaning test build artifacts"
)