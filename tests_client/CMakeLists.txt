find_package(Boost REQUIRED COMPONENTS system filesystem program_options)
include_directories(.)

set(SOURCES
        I2PdUtils.cpp
        CliParser.cpp
        StreamFactory.cpp
        NormalStreamingImpl.cpp
        SimpleStreamingImpl.cpp
        ConnectionUtils.cpp
)

add_executable(embedded_stream_itest embedded_stream_itest.cpp ${SOURCES})
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

set(SOURCES_FTP
        ChunkedFileClient.cpp
        ChunkedFileServer.cpp
        FileTransferFactory.cpp
        SimpleMessagingFileClient.cpp
        NormalStreamingFileClient.cpp
        NormalStreamingFileServer.cpp
        BinaryFileProtocol.cpp
)

add_executable(chunked_file_test chunked_file_test.cpp ${SOURCES} ${SOURCES_FTP})
target_include_directories(embedded_stream_itest PRIVATE
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