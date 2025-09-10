cmake_minimum_required(VERSION 3.12)
project(i2pd_tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Boost REQUIRED COMPONENTS system filesystem program_options)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)  
find_package(Threads REQUIRED)

# Include directories  
include_directories(../tests_client ../tests_client/core ../tests_client/transport ../tests_client/filetransfer ../tests_client/app)
include_directories(${CMAKE_SOURCE_DIR}/libi2pd_client)
include_directories(${CMAKE_SOURCE_DIR}/libi2pd)

# Core library sources (from tests_client directory)
set(CORE_SOURCES
    ../tests_client/core/I2PdUtils.cpp
    ../tests_client/core/ConnectionUtils.cpp
    ../tests_client/core/ErrorHandling.cpp
    ../tests_client/core/OperationContext.cpp
)

# Transport layer sources
set(TRANSPORT_SOURCES
    ../tests_client/transport/StreamFactory.cpp
    ../tests_client/transport/NormalStreamingImpl.cpp
    ../tests_client/transport/SimpleStreamingImpl.cpp
    ../tests_client/transport/SimpleStreamingProvider.cpp
    ../tests_client/transport/NormalStreamingProvider.cpp
    ../tests_client/transport/StreamingProviderRegistry.cpp
)

# File transfer sources  
set(FILETRANSFER_SOURCES
    ../tests_client/filetransfer/ChunkedFileClient.cpp
    ../tests_client/filetransfer/ChunkedFileServer.cpp
    ../tests_client/filetransfer/FileTransferFactory.cpp
    ../tests_client/filetransfer/SimpleMessagingFileClient.cpp
    ../tests_client/filetransfer/NormalStreamingFileClient.cpp
    ../tests_client/filetransfer/NormalStreamingFileServer.cpp
    ../tests_client/filetransfer/BinaryFileProtocol.cpp
    ../tests_client/filetransfer/TransferStrategies.cpp
    ../tests_client/filetransfer/TransferStrategyRegistry.cpp
    ../tests_client/filetransfer/TransferSubject.cpp
    ../tests_client/filetransfer/TransferCommands.cpp
    ../tests_client/filetransfer/TransferCommandQueue.cpp
    ../tests_client/filetransfer/TransferConfigBuilder.cpp
)

# Application sources
set(APP_SOURCES
    ../tests_client/app/CliParser.cpp
)

# Common libraries for all tests
set(COMMON_LIBRARIES
    libi2pdclient
    libi2pd
    libi2pdlang
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