find_package(Boost REQUIRED COMPONENTS system filesystem program_options)

add_executable(embedded_stream_itest embedded_stream_itest.cpp)
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