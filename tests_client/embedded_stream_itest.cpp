#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "Config.h"
#include "CliParser.h"
#include "I2PdUtils.h"
#include "StreamInterface.h"

using namespace std::chrono_literals;
using namespace i2p::embed;

namespace {
    std::atomic g_running{true};
    void onSignal(int) { g_running = false; }
}

int main(int argc, char** argv) {
    try {
        // Parse command line arguments
        auto args = CliParser::parse(argc, argv);

        // Set up signal handlers
        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        // Initialize i2pd configuration
        i2p::config::Init();
        I2PdUtils::initNode(args.mode, args.datadir, args.conf);
        I2PdUtils::startCore();

        // Determine protocol type
        auto protocolType = args.useSimpleProtocol ?
            StreamFactory::ProtocolType::SIMPLE_MESSAGING :
            StreamFactory::ProtocolType::NORMAL_STREAMING;

        std::string protocolName = args.useSimpleProtocol ? "SimpleSend/SimpleReceive" : "Normal Streaming";
        std::cout << "Using protocol: " << protocolName << std::endl;

        if (args.mode == "server") {
            // SERVER MODE
            std::cout << "Starting server..." << std::endl;

            // Create server destination (public, with LeaseSet)
            auto serverDest = I2PdUtils::createDestination(true);

            // Create server instance
            auto server = StreamFactory::createServer(protocolType, serverDest);

            // Define message handler
            auto messageHandler = [](const std::string& message) -> std::string {
                std::cout << "Server received: [" << message << "]" << std::endl;
                return "server echo: " + message;
            };

            // Start server
            server->start(messageHandler);

            if (!server->isReady()) {
                std::cerr << "Failed to start server: " << server->getStatus() << std::endl;
                return 1;
            }

            std::cout << "Server ready!" << std::endl;
            std::cout << "Server b32: " << server->getB32Address() << std::endl;
            std::cout << "Status: " << server->getStatus() << std::endl;

            // Wait for shutdown signal
            while (g_running) {
                std::this_thread::sleep_for(250ms);
            }

            std::cout << "Shutting down server..." << std::endl;
            server->stop();

        } else {
            // CLIENT MODE
            std::cout << "Starting client..." << std::endl;
            std::cout << "Target server: " << args.server_b32 << ".b32.i2p" << std::endl;
            std::cout << "Message to send: [" << args.message << "]" << std::endl;

            // Create client destination (private, no LeaseSet publishing)
            auto clientDest = I2PdUtils::createDestination(false);

            // Wait for client destination to be ready
            if (!I2PdUtils::waitForDestinationReady(clientDest, 10000)) {
                std::cerr << "Client destination failed to become ready" << std::endl;
                return 1;
            }

            // Create client instance
            auto client = StreamFactory::createClient(protocolType, clientDest);

            if (!client->isReady()) {
                std::cerr << "Client not ready: " << client->getStatus() << std::endl;
                return 1;
            }

            std::cout << "Client ready, sending message..." << std::endl;
            std::cout << "Client status: " << client->getStatus() << std::endl;

            // Send message and get response
            std::string response = client->sendMessage(args.server_b32, args.message, 15000);

            if (!response.empty()) {
                std::cout << "SUCCESS: Received response: [" << response << "]" << std::endl;
                std::cout << "Client status: " << client->getStatus() << std::endl;

                // Brief wait before shutdown
                std::this_thread::sleep_for(2s);

            } else {
                std::cerr << "FAILED: No response received" << std::endl;
                std::cerr << "Client status: " << client->getStatus() << std::endl;
                return 1;
            }
        }

        // Clean shutdown
        I2PdUtils::stopCore();
        std::cout << "Shutdown complete." << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "FATAL: " << e.what() << std::endl;
        try {
            I2PdUtils::stopCore();
        } catch (...) {}
        return 1;
    }
}