cmake_minimum_required(VERSION 3.12)
project(i2pd_performance_tests)

find_package(Boost REQUIRED COMPONENTS system filesystem program_options)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

include_directories(. core transport filetransfer app)
include_directories(${CMAKE_SOURCE_DIR}/../libi2pd_client)
include_directories(${CMAKE_SOURCE_DIR}/../libi2pd)

# Core utilities
set(CORE_SOURCES
        core/I2PdUtils.cpp
        core/ConnectionUtils.cpp
        core/ErrorHandling.cpp
        core/OperationContext.cpp
        core/NetworkDetector.cpp
        core/UniversalFlowControl.cpp
)

# Transport layer
set(TRANSPORT_SOURCES
        transport/StreamFactory.cpp
        transport/NormalStreamingImpl.cpp
        transport/SimpleStreamingImpl.cpp
        transport/SimpleStreamingProvider.cpp
        transport/NormalStreamingProvider.cpp
        transport/StreamingProviderRegistry.cpp
)

# Performance optimization layer (Session 10)
set(PERFORMANCE_SOURCES
        # Header-only performance modules - no .cpp files needed
        # AdaptiveCompression.h
        # BulkTransferOptimization.h  
        # FileTypeDetection.h
        # PerformanceOptimizedStreaming.h
)

# File transfer layer
set(FILETRANSFER_SOURCES
        filetransfer/ChunkedFileClient.cpp
        filetransfer/ChunkedFileServer.cpp
        filetransfer/FileTransferFactory.cpp
        filetransfer/SimpleMessagingFileClient.cpp
        filetransfer/NormalStreamingFileClient.cpp
        filetransfer/NormalStreamingFileServer.cpp
        filetransfer/BinaryFileProtocol.cpp
        filetransfer/TransferStrategies.cpp
        filetransfer/TransferStrategyRegistry.cpp
        filetransfer/TransferSubject.cpp
        filetransfer/TransferCommands.cpp
        filetransfer/TransferCommandQueue.cpp
        filetransfer/TransferConfigBuilder.cpp
)

# Application layer
set(APP_SOURCES
        app/CliParser.cpp
)

add_executable(embedded_stream_itest app/embedded_stream_itest.cpp ${CORE_SOURCES} ${TRANSPORT_SOURCES} ${APP_SOURCES})
target_include_directories(embedded_stream_itest PRIVATE
        ${CMAKE_SOURCE_DIR}/libi2pd_client
        ${CMAKE_SOURCE_DIR}/libi2pd
)

target_link_libraries(embedded_stream_itest
        libi2pdclient
        libi2pd
        libi2pdlang
        Boost::system Boost::filesystem Boost::program_options
        OpenSSL::SSL OpenSSL::Crypto
        ZLIB::ZLIB
        Threads::Threads
)

add_executable(chunked_file_test app/chunked_file_test.cpp ${CORE_SOURCES} ${TRANSPORT_SOURCES} ${FILETRANSFER_SOURCES} ${APP_SOURCES})
target_include_directories(chunked_file_test PRIVATE
        ${CMAKE_SOURCE_DIR}/libi2pd_client
        ${CMAKE_SOURCE_DIR}/libi2pd
)

target_link_libraries(chunked_file_test
        libi2pdclient
        libi2pd
        libi2pdlang
        Boost::system Boost::filesystem Boost::program_options
        OpenSSL::SSL OpenSSL::Crypto
        ZLIB::ZLIB
        Threads::Threads
)
