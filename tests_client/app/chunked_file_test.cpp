#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <iomanip>

#include "../core/I2PdUtils.h"
#include "../filetransfer/FileTransferFactory.h"
#include "../filetransfer/IFileTransfer.h"
#include "../core/TransferConfig.h"
#include "../core/FileTransferLogging.h"
#include "Config.h"
#include "FS.h"
#include "Log.h"

using namespace i2p::filetransfer;
using namespace std::chrono_literals;

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <mode> [options]\n\n";
    std::cout << "Modes:\n";
    std::cout << "  server [--conf config.conf] [--datadir path] [--file filename:size] [--hops N] [--simple|--normal]\n";
    std::cout << "  client [--conf config.conf] [--datadir path] --server-b32 <address> --file <filename> [--hops N] [--simple|--normal]\n\n";
    std::cout << "Server Options:\n";
    std::cout << "  --conf <file>       Configuration file path\n";
    std::cout << "  --datadir <path>    Data directory path\n";
    std::cout << "  --file <name:size>  Generate mock file (default: " << TransferConfig::getDefaultMockFileName() << ":" << TransferConfig::getDefaultMockFileSize() << " = " << (TransferConfig::getDefaultMockFileSize() / (1024*1024)) << "MB)\n";
    std::cout << "  --hops <N>          Tunnel hop count (default: 1, 0 = direct connection)\n";
    std::cout << "  --simple            Use Simple Messaging protocol (default, reliable)\n";
    std::cout << "  --normal            Use Normal Streaming protocol (higher performance)\n\n";
    std::cout << "Client Options:\n";
    std::cout << "  --conf <file>       Configuration file path\n";
    std::cout << "  --datadir <path>    Data directory path\n";
    std::cout << "  --server-b32 <addr> Server's base32 address\n";
    std::cout << "  --file <filename>   File to download (default: test.bin)\n";
    std::cout << "  --timeout <ms>      Transfer timeout in milliseconds (default: " << TransferConfig::getTransferTimeout() << ")\n";
    std::cout << "  --hops <N>          Tunnel hop count (default: 1, 0 = direct connection)\n";
    std::cout << "  --simple            Use Simple Messaging protocol (default, reliable)\n";
    std::cout << "  --normal            Use Normal Streaming protocol (higher performance)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  # 0-hop (direct) connection - only floodfill needed\n";
    std::cout << "  " << program << " server --conf srv.conf --datadir /tmp/srv --hops 0 --simple\n";
    std::cout << "  " << program << " client --conf cli.conf --datadir /tmp/cli --hops 0 --file test.bin --simple\n\n";
    std::cout << "  # Multi-hop connection - requires floodfill + peer routers\n";
    std::cout << "  " << program << " server --conf srv.conf --datadir /tmp/srv --hops 2 --normal\n";
    std::cout << "  " << program << " client --conf cli.conf --datadir /tmp/cli --hops 2 --file test.bin --normal\n\n";
    std::cout << "Note: For hop counts > 0, start floodfill + peer routers first:\n";
    std::cout << "  ~/i2pd/cmake-debug/i2pd --datadir=/tmp/i2pd-ff --conf=~/i2pd-conf/ff.conf &\n";
    std::cout << "  ~/i2pd/cmake-debug/i2pd --datadir=/tmp/peer1 --conf=~/i2pd-conf/peer1.conf &\n";
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
    auto protocol = TransferProtocol::SIMPLE_MESSAGING; // Default to simple messaging
    int hopCount = 1; // Default hop count
    
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
        } else if (arg == "--hops" && i + 1 < argc) {
            hopCount = std::stoi(argv[++i]);
        } else if (arg == "--simple") {
            protocol = TransferProtocol::SIMPLE_MESSAGING;
        } else if (arg == "--normal") {
            protocol = TransferProtocol::NORMAL_STREAMING;
        }
    }
    
    try {
        std::cout << "Initializing i2pd node...\n";
        i2p::embed::I2PdUtils::initNode("chunked-server", dataDir, configPath);
        
        // Configure exploratory tunnels with specified hop count
        i2p::embed::I2PdUtils::configureExploratoryTunnels(hopCount);
        
        i2p::embed::I2PdUtils::startCore();
        
        std::cout << "Creating server destination with " << hopCount << " hops...\n";
        
        auto serverDest = i2p::embed::I2PdUtils::createDestination(true, hopCount); // public
        
        // Save B32 address immediately after destination creation (before network readiness)
        if (serverDest) {
            std::string b32Address = serverDest->GetIdentHash().ToBase32() + ".b32.i2p";
            std::string b32File = "server_b32.txt";
            
            // Save to both current directory and datadir for flexibility
            std::vector<std::string> savePaths = {
                b32File,  // Current directory
                dataDir + "/" + b32File,  // Data directory
                "/tmp/server_b32.txt"  // Common temp location
            };
            
            for (const auto& path : savePaths) {
                try {
                    std::ofstream file(path);
                    if (file.is_open()) {
                        file << b32Address << std::endl;
                        file.close();
                        std::cout << "B32 address saved to: " << path << "\n";
                    }
                } catch (const std::exception& e) {
                    // Ignore save errors, continue with other paths
                }
            }
            
            std::cout << "\n=== SERVER B32 ADDRESS SAVED ===\n";
            std::cout << "Server Address: " << b32Address << "\n";
            std::cout << "🎯 Client can now connect automatically!\n\n";
        }
        
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
        
        // Generate mock file with specified size
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
        std::cout << "\nClient can now connect automatically without copy-paste!\n";
        std::cout << "   Just run: " << argv[0] << " client --conf <config> --datadir <dir> --file <filename>\n";
        std::cout << "Press Ctrl+C to stop...\n\n";
        
        // Keep server running
        while (server->isReady()) {
            std::this_thread::sleep_for(1000ms);
        }
        
        std::cout << "Server shutting down...\n";
        server->stop();
        i2p::embed::I2PdUtils::shutdownRecoverySystems();
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
    int hopCount = 1; // Default hop count
    
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
        } else if (arg == "--hops" && i + 1 < argc) {
            hopCount = std::stoi(argv[++i]);
        } else if (arg == "--simple") {
            protocol = TransferProtocol::SIMPLE_MESSAGING;
        } else if (arg == "--normal") {
            protocol = TransferProtocol::NORMAL_STREAMING;
        }
    }
    
    // Auto-load B32 address from file if not provided
    if (serverB32.empty()) {
        std::cout << "No --server-b32 provided, attempting to load from saved file...\n";
        
        // Try multiple locations for the B32 file
        std::vector<std::string> searchPaths = {
            "server_b32.txt",  // Current directory
            dataDir + "/server_b32.txt",  // Data directory 
            "/tmp/server_b32.txt",  // Common temp location
            "../data/output/server_b32.txt"  // Test output directory
        };
        
        for (const auto& path : searchPaths) {
            std::ifstream file(path);
            if (file.is_open()) {
                if (std::getline(file, serverB32) && !serverB32.empty()) {
                    // Remove any whitespace
                    serverB32.erase(serverB32.find_last_not_of(" \t\r\n") + 1);
                    if (!serverB32.empty() && serverB32.find(".b32.i2p") != std::string::npos) {
                        std::cout << "Loaded server B32 from: " << path << "\n";
                        std::cout << "   Server address: " << serverB32 << "\n";
                        break;
                    }
                }
                file.close();
            }
        }
        
        if (serverB32.empty()) {
            std::cerr << "ERROR: Could not find server B32 address!\n";
            std::cerr << "Either:\n";
            std::cerr << "1. Start the server first to generate server_b32.txt, or\n";
            std::cerr << "2. Provide --server-b32 <address> manually\n\n";
            std::cerr << "Searched in:\n";
            for (const auto& path : searchPaths) {
                std::cerr << "  - " << path << "\n";
            }
            printUsage(argv[0]);
            return 1;
        }
    }
    
    try {
        std::cout << "Initializing i2pd node...\n";
        i2p::embed::I2PdUtils::initNode("chunked-client", dataDir, configPath);
        
        // Configure exploratory tunnels with specified hop count
        i2p::embed::I2PdUtils::configureExploratoryTunnels(hopCount);
        
        i2p::embed::I2PdUtils::startCore();
        
        std::cout << "Creating client destination with " << hopCount << " hops...\n";
        
        auto clientDest = i2p::embed::I2PdUtils::createDestination(false, hopCount); // private
        
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
        i2p::embed::I2PdUtils::shutdownRecoverySystems();
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