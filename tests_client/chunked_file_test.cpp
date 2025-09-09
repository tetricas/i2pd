#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <iomanip>

#include "I2PdUtils.h"
#include "FileTransferFactory.h"
#include "IFileTransfer.h"
#include "TransferConfig.h"
#include "FileTransferLogging.h"
#include "Config.h"
#include "FS.h"
#include "Log.h"

using namespace i2p::filetransfer;
using namespace std::chrono_literals;

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <mode> [options]\n\n";
    std::cout << "Modes:\n";
    std::cout << "  server [--conf config.conf] [--datadir path] [--file filename:size] [--simple|--normal]\n";
    std::cout << "  client [--conf config.conf] [--datadir path] --server-b32 <address> --file <filename> [--simple|--normal]\n\n";
    std::cout << "Server Options:\n";
    std::cout << "  --conf <file>       Configuration file path\n";
    std::cout << "  --datadir <path>    Data directory path\n";
    std::cout << "  --file <name:size>  Generate mock file (default: " << TransferConfig::getDefaultMockFileName() << ":" << TransferConfig::getDefaultMockFileSize() << " = " << (TransferConfig::getDefaultMockFileSize() / (1024*1024)) << "MB)\n";
    std::cout << "  --simple            Use Simple Messaging protocol (default, reliable)\n";
    std::cout << "  --normal            Use Normal Streaming protocol (higher performance)\n\n";
    std::cout << "Client Options:\n";
    std::cout << "  --conf <file>       Configuration file path\n";
    std::cout << "  --datadir <path>    Data directory path\n";
    std::cout << "  --server-b32 <addr> Server's base32 address\n";
    std::cout << "  --file <filename>   File to download (default: test.bin)\n";
    std::cout << "  --timeout <ms>      Transfer timeout in milliseconds (default: " << TransferConfig::getTransferTimeout() << ")\n";
    std::cout << "  --simple            Use Simple Messaging protocol (default, reliable)\n";
    std::cout << "  --normal            Use Normal Streaming protocol (higher performance)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  # Start server with 100MB test file using Simple Messaging\n";
    std::cout << "  " << program << " server --conf srv.conf --datadir /tmp/srv --simple\n\n";
    std::cout << "  # Start client using Normal Streaming\n";
    std::cout << "  " << program << " client --conf cli.conf --datadir /tmp/cli \\\n";
    std::cout << "    --server-b32 abc123...xyz.b32.i2p --file test.bin --normal\n";
}

void printTransferStats(const IFileTransferClient::TransferResult& result) {
    FT_LOG_INFO("Statistics", "\n=== TRANSFER STATISTICS ===");
    FT_LOG_INFO("Statistics", "Success: " << (result.success ? "YES" : "NO"));
    
    if (!result.success) {
        FT_LOG_ERROR("Statistics", "Error: " << result.error);
        return;
    }
    
    const auto& stats = result.stats;
    
    FT_LOG_INFO("Statistics", "Initialization Time: " << stats.getInitTime().count() << " ms");
    FT_LOG_INFO("Statistics", "Request Time: " << stats.getRequestTime().count() << " ms");
    FT_LOG_INFO("Statistics", "Total Transfer Time: " << stats.getTransferTime().count() << " ms");
    FT_LOG_INFO("Statistics", "Total Bytes: " << stats.totalBytes << " bytes (" << 
                std::fixed << std::setprecision(2) << (stats.totalBytes / 1024.0) << " KB)");
    FT_LOG_INFO("Statistics", "Chunks: " << stats.chunksReceived << "/" << stats.chunksTotal);
    FT_LOG_INFO("Statistics", "Throughput: " << std::fixed << std::setprecision(2) << 
                stats.getThroughputKBps() << " KB/s");
    FT_LOG_INFO("Statistics", "Average Chunk Time: " << stats.getAverageChunkTime().count() << " ms");
    FT_LOG_INFO("Statistics", "Data Verified: " << (stats.verified ? "YES" : "NO"));
    
    if (!stats.chunkTimes.empty()) {
        auto minTime = *std::min_element(stats.chunkTimes.begin(), stats.chunkTimes.end());
        auto maxTime = *std::max_element(stats.chunkTimes.begin(), stats.chunkTimes.end());
        FT_LOG_INFO("Statistics", "Chunk Time Range: " << minTime.count() << " - " << maxTime.count() << " ms");
    }
    
    FT_LOG_INFO("Statistics", "===========================\n");
}

int runServer(int argc, char* argv[]) {
    std::string configPath;
    std::string dataDir;
    std::string mockFileName = TransferConfig::getDefaultMockFileName();
    size_t mockFileSize = TransferConfig::getDefaultMockFileSize();
    TransferProtocol protocol = TransferProtocol::SIMPLE_MESSAGING; // Default to simple messaging
    
    // Parse server arguments
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--conf" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--datadir" && i + 1 < argc) {
            dataDir = argv[++i];
        } else if (arg == "--file" && i + 1 < argc) {
            std::string fileSpec = argv[++i];
            auto colonPos = fileSpec.find(':');
            if (colonPos != std::string::npos) {
                mockFileName = fileSpec.substr(0, colonPos);
                mockFileSize = std::stoul(fileSpec.substr(colonPos + 1));
            } else {
                mockFileName = fileSpec;
            }
        } else if (arg == "--simple") {
            protocol = TransferProtocol::SIMPLE_MESSAGING;
        } else if (arg == "--normal") {
            protocol = TransferProtocol::NORMAL_STREAMING;
        }
    }
    
    try {
        std::cout << "Initializing i2pd node...\n";
        i2p::config::Init();
        i2p::embed::I2PdUtils::initNode("chunked-server", dataDir, configPath);
        i2p::embed::I2PdUtils::startCore();
        
        std::cout << "Creating server destination...\n";
        auto serverDest = i2p::embed::I2PdUtils::createDestination(true); // public
        
        if (!i2p::embed::I2PdUtils::waitForDestinationReady(serverDest, TransferConfig::getConnectionTimeout())) {
            std::cerr << "ERROR: Server destination not ready\n";
            return 1;
        }
        
        std::cout << "Starting file server with " << FileTransferFactory::getProtocolName(protocol) << " protocol...\n";
        auto server = FileTransferFactory::createServer(protocol, serverDest);
        
        if (!server) {
            std::cerr << "ERROR: Failed to create server\n";
            return 1;
        }
        
        // Generate mock file
        std::cout << "Generating mock file: " << mockFileName << " (" << mockFileSize << " bytes)\n";
        server->generateMockFile(mockFileName, mockFileSize, mockFileName);
        
        server->start();
        
        if (!server->isReady()) {
            std::cerr << "ERROR: Server failed to start\n";
            return 1;
        }
        
        std::cout << "\n=== SERVER READY ===\n";
        std::cout << "Protocol: " << FileTransferFactory::getProtocolName(protocol) << "\n";
        std::cout << "Server Address: " << server->getB32Address() << "\n";
        std::cout << "Available Files:\n";
        for (const auto& filename : server->getFileList()) {
            std::cout << "  - " << filename << "\n";
        }
        std::cout << "Status: " << server->getStatus() << "\n";
        std::cout << "Press Ctrl+C to stop...\n\n";
        
        // Keep server running
        while (server->isReady()) {
            std::this_thread::sleep_for(1000ms);
        }
        
        std::cout << "Server shutting down...\n";
        server->stop();
        i2p::embed::I2PdUtils::stopCore();
        
    } catch (const std::exception& e) {
        std::cerr << "Server exception: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}

int runClient(int argc, char* argv[]) {
    std::string configPath;
    std::string dataDir;
    std::string serverB32;
    std::string filename = TransferConfig::getDefaultMockFileName();
    int timeout = TransferConfig::getTransferTimeout();
    TransferProtocol protocol = TransferProtocol::SIMPLE_MESSAGING; // Default to simple messaging
    
    // Parse client arguments
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--conf" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--datadir" && i + 1 < argc) {
            dataDir = argv[++i];
        } else if (arg == "--server-b32" && i + 1 < argc) {
            serverB32 = argv[++i];
        } else if (arg == "--file" && i + 1 < argc) {
            filename = argv[++i];
        } else if (arg == "--timeout" && i + 1 < argc) {
            timeout = std::stoi(argv[++i]);
        } else if (arg == "--simple") {
            protocol = TransferProtocol::SIMPLE_MESSAGING;
        } else if (arg == "--normal") {
            protocol = TransferProtocol::NORMAL_STREAMING;
        }
    }
    
    if (serverB32.empty()) {
        std::cerr << "ERROR: --server-b32 is required for client mode\n";
        printUsage(argv[0]);
        return 1;
    }
    
    try {
        std::cout << "Initializing i2pd node...\n";
        i2p::config::Init();
        i2p::embed::I2PdUtils::initNode("chunked-client", dataDir, configPath);
        i2p::embed::I2PdUtils::startCore();
        
        std::cout << "Creating client destination...\n";
        auto clientDest = i2p::embed::I2PdUtils::createDestination(false); // private
        
        if (!i2p::embed::I2PdUtils::waitForDestinationReady(clientDest, TransferConfig::getConnectionTimeout())) {
            std::cerr << "ERROR: Client destination not ready\n";
            return 1;
        }
        
        std::cout << "Starting file client with " << FileTransferFactory::getProtocolName(protocol) << " protocol...\n";
        auto client = FileTransferFactory::createClient(protocol, clientDest);
        
        if (!client) {
            std::cerr << "ERROR: Failed to create client\n";
            return 1;
        }
        
        if (!client->isReady()) {
            std::cerr << "ERROR: Client not ready\n";
            return 1;
        }
        
        FT_LOG_INFO("Client", "\n=== STARTING FILE TRANSFER ===");
        FT_LOG_INFO("Client", "Protocol: " << FileTransferFactory::getProtocolName(protocol));
        FT_LOG_INFO("Client", "Server: " << serverB32);
        FT_LOG_INFO("Client", "File: " << filename);
        FT_LOG_INFO("Client", "Timeout: " << timeout << " ms");
        FT_LOG_INFO("Client", "Client Status: " << client->getStatus());
        
        // Request file transfer
        auto result = client->downloadFile(serverB32, filename, timeout);
        
        // Print results
        printTransferStats(result);
        
        if (result.success) {
            FT_LOG_INFO("Client", "SUCCESS: File transfer completed successfully!");
            FT_LOG_INFO("Client", "Downloaded " << result.data.size() << " bytes in " << 
                        result.stats.getTransferTime().count() << " ms");
        } else {
            FT_LOG_ERROR("Client", "FAILED: " << result.error);
        }
        
        FT_LOG_INFO("Client", "Client shutting down...");
        i2p::embed::I2PdUtils::stopCore();
        
        return result.success ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cerr << "Client exception: " << e.what() << "\n";
        return 1;
    }
}

int main(int argc, char* argv[]) {
    try {
        // Initialize transfer configuration defaults
        TransferConfig::initializeDefaults();
        
        if (argc < 2) {
            printUsage(argv[0]);
            return 1;
        }

        const std::string mode = argv[1];
        if (mode == "server")
            return runServer(argc, argv);

        if (mode == "client")
            return runClient(argc, argv);

        std::cerr << "ERROR: Unknown mode '" << mode << "'\n";
        printUsage(argv[0]);
        return 1;
        
    } catch (const std::exception& e) {
        FT_LOG_ERROR("Main", "Application error: " << e.what());
        return 1;
    }
}