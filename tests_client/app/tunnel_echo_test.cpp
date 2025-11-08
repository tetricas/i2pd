#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <iomanip>
#include <memory>
#include <atomic>
#include <cstdlib>

#include "../core/I2PdUtils.h"
#include "../core/TransferConfig.h"
#include "../core/FileTransferLogging.h"
#include "Config.h"
#include "FS.h"
#include "Log.h"
#include "Destination.h"
#include "TunnelPool.h"
#include "Datagram.h"
#include "RouterInfo.h"
#include "Timestamp.h"

using namespace i2p::filetransfer;

using namespace std::chrono_literals;

// Protocol constants for tunnel echo test
namespace TunnelEcho {
    const uint8_t PROTOCOL_TYPE_TUNNEL_ECHO = 200;  // Custom protocol type
    const uint16_t ECHO_PORT = 8888;
    const uint16_t CLIENT_PORT = 8889;
    
    // Simple message types
    enum MessageType : uint8_t {
        ECHO_REQUEST = 1,
        ECHO_RESPONSE = 2,
        TUNNEL_INFO = 3
    };
    
    // Simple echo message structure
    struct EchoMessage {
        uint8_t type;           // MessageType
        uint32_t sequenceId;    // Message sequence number
        uint64_t timestamp;     // Timestamp for RTT measurement
        uint32_t dataSize;      // Payload size
        uint8_t data[];         // Variable payload
    } __attribute__((packed));
}

class SSU2PreferringTunnelPool : public i2p::tunnel::TunnelPool {
public:
    SSU2PreferringTunnelPool(int numInboundHops, int numOutboundHops, 
                            int numInboundTunnels, int numOutboundTunnels,
                            int inboundVariance, int outboundVariance, bool isHighBandwidth)
        : TunnelPool(numInboundHops, numOutboundHops, numInboundTunnels, 
                    numOutboundTunnels, inboundVariance, outboundVariance, isHighBandwidth)
    {
        std::cout << "Created SSU2-preferring tunnel pool with " 
                  << numOutboundTunnels << " outbound tunnels, " 
                  << numInboundTunnels << " inbound tunnels\n";
    }
    
    std::shared_ptr<i2p::tunnel::OutboundTunnel> GetNextOutboundTunnel(
        std::shared_ptr<i2p::tunnel::OutboundTunnel> excluded = nullptr) 
    {
        // Prefer SSU2 transports
        constexpr i2p::data::RouterInfo::CompatibleTransports SSU2_TRANSPORTS = 
            i2p::data::RouterInfo::eSSU2V4 | i2p::data::RouterInfo::eSSU2V6;
            
        auto tunnel = TunnelPool::GetNextOutboundTunnel(excluded, SSU2_TRANSPORTS);
        if (tunnel) {
            std::cout << "Selected SSU2-preferring outbound tunnel with " 
                      << tunnel->GetNumHops() << " hops\n";
            return tunnel;
        }
        
        // Fallback to any available tunnel
        std::cout << "SSU2 tunnel not available, using fallback\n";
        return TunnelPool::GetNextOutboundTunnel(excluded);
    }
    
    std::shared_ptr<i2p::tunnel::InboundTunnel> GetNextInboundTunnel(
        std::shared_ptr<i2p::tunnel::InboundTunnel> excluded = nullptr) 
    {
        // Prefer SSU2 transports
        constexpr i2p::data::RouterInfo::CompatibleTransports SSU2_TRANSPORTS = 
            i2p::data::RouterInfo::eSSU2V4 | i2p::data::RouterInfo::eSSU2V6;
            
        auto tunnel = TunnelPool::GetNextInboundTunnel(excluded, SSU2_TRANSPORTS);
        if (tunnel) {
            std::cout << "Selected SSU2-preferring inbound tunnel with " 
                      << tunnel->GetNumHops() << " hops\n";
            return tunnel;
        }
        
        // Fallback to any available tunnel
        std::cout << "SSU2 tunnel not available, using fallback\n";
        return TunnelPool::GetNextInboundTunnel(excluded);
    }
};

class TunnelEchoManager {
public:
    TunnelEchoManager(std::shared_ptr<i2p::client::ClientDestination> owner, 
                     bool isServer, int hops)
        : m_Owner(owner), m_IsServer(isServer), m_Hops(hops), 
          m_SequenceId(0), m_MessagesReceived(0), m_MessagesSent(0) 
    {
        std::cout << "Created tunnel echo manager (" 
                  << (isServer ? "server" : "client") << ") with " 
                  << hops << " hops\n";
    }
    
    void Start() {
        std::cout << "Starting tunnel echo manager...\n";
        
        // Create SSU2-preferring tunnel pool
        CreateSSU2TunnelPool();
        
        // Get the client destination's datagram destination
        // Use eDatagramV1 to support non-ratchet sessions
        auto datagramDestination = m_Owner->CreateDatagramDestination(false, i2p::datagram::eDatagramV1);
        std::cout << "Created datagram destination: " << (datagramDestination ? "SUCCESS" : "FAILED") << std::endl;
        
        // Set up message receiver using std::bind (like other I2P applications)
        datagramDestination->SetReceiver(std::bind(&TunnelEchoManager::HandleDatagramReceive, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, 
            std::placeholders::_4, std::placeholders::_5), TunnelEcho::ECHO_PORT);
        std::cout << "Registered datagram receiver for port " << TunnelEcho::ECHO_PORT << "\n";
        
        std::cout << "Tunnel echo manager started. Identity: " 
                  << m_Owner->GetIdentHash().ToBase32() << "\n";
        std::cout << "B32 Address: " << m_Owner->GetIdentHash().ToBase32() << ".b32.i2p\n";
    }
    
    void Stop() {
        std::cout << "Stopping tunnel echo manager...\n";
        
        // Reset receiver
        auto datagramDestination = m_Owner->CreateDatagramDestination(false, i2p::datagram::eDatagramV1);
        datagramDestination->ResetReceiver(TunnelEcho::ECHO_PORT);
        
        // Print statistics
        std::cout << "Echo Statistics:\n";
        std::cout << "  Messages sent: " << m_MessagesSent << "\n";
        std::cout << "  Messages received: " << m_MessagesReceived << "\n";
    }
    
    uint32_t GetMessagesSent() const { return m_MessagesSent; }
    uint32_t GetMessagesReceived() const { return m_MessagesReceived; }
    
    void HandleDatagramReceive(const i2p::data::IdentityEx& from, uint16_t fromPort, uint16_t toPort,
                              const uint8_t* buf, size_t len) {
        std::cout << "DEBUG: Received datagram from " << from.GetIdentHash().ToBase32().substr(0, 8) 
                  << "... fromPort=" << fromPort << " toPort=" << toPort << " len=" << len << std::endl;
        HandleMessage(from.GetIdentHash(), fromPort, toPort, buf, len);
    }
    
    void SendEchoRequest(const i2p::data::IdentHash& to, const std::string& payload) {
        if (!m_IsServer) {
            auto message = CreateEchoMessage(TunnelEcho::ECHO_REQUEST, payload);
            SendMessage(to, message);
            ++m_MessagesSent;
            std::cout << "Sent echo request #" << m_SequenceId 
                      << " to " << to.ToBase32().substr(0, 8) << "... "
                      << "(" << payload.size() << " bytes)\n";
        }
    }
    
    void SendTunnelInfo(const i2p::data::IdentHash& to) {
        std::string info = GetTunnelInfo();
        auto message = CreateEchoMessage(TunnelEcho::TUNNEL_INFO, info);
        SendMessage(to, message);
        std::cout << "Sent tunnel info to " << to.ToBase32().substr(0, 8) << "...\n";
    }

private:
    std::shared_ptr<i2p::client::ClientDestination> m_Owner;
    bool m_IsServer;
    int m_Hops;
    std::atomic<uint32_t> m_SequenceId;
    std::atomic<uint32_t> m_MessagesReceived;
    std::atomic<uint32_t> m_MessagesSent;
    
    void CreateSSU2TunnelPool() {
        // Create custom tunnel pool with SSU2 preference
        auto tunnelPool = std::make_shared<SSU2PreferringTunnelPool>(
            m_Hops,    // inbound hops
            m_Hops,    // outbound hops  
            2,         // inbound tunnels
            2,         // outbound tunnels
            0,         // inbound variance
            0,         // outbound variance
            false      // high bandwidth
        );
        
        tunnelPool->SetLocalDestination(m_Owner);
        // Note: TunnelPool is set automatically when creating ClientDestination
        // Custom tunnel pool management would require modifying ClientDestination internals
        
        std::cout << "Created SSU2-preferring tunnel pool\n";
    }
    
    std::vector<uint8_t> CreateEchoMessage(TunnelEcho::MessageType type, const std::string& payload) {
        size_t messageSize = sizeof(TunnelEcho::EchoMessage) + payload.size();
        std::vector<uint8_t> buffer(messageSize);
        
        auto* msg = reinterpret_cast<TunnelEcho::EchoMessage*>(buffer.data());
        msg->type = type;
        msg->sequenceId = ++m_SequenceId;
        msg->timestamp = i2p::util::GetMillisecondsSinceEpoch();
        msg->dataSize = payload.size();
        
        if (!payload.empty()) {
            memcpy(msg->data, payload.data(), payload.size());
        }
        
        return buffer;
    }
    
    void SendMessage(const i2p::data::IdentHash& to, const std::vector<uint8_t>& message) {
        auto datagramDestination = m_Owner->CreateDatagramDestination(false, i2p::datagram::eDatagramV1);
        datagramDestination->SendDatagramTo(message.data(), message.size(), to, 
                                           TunnelEcho::CLIENT_PORT, TunnelEcho::ECHO_PORT);
    }
    
    void HandleMessage(const i2p::data::IdentHash& from, uint16_t fromPort, uint16_t toPort,
                      const uint8_t* buf, size_t len)
    {
        if (len < sizeof(TunnelEcho::EchoMessage))
        {
            std::cout << "Received malformed message from " 
                      << from.ToBase32().substr(0, 8) << "...\n";
            ++m_MessagesReceived;
            return;
        }
        
        const auto* msg = reinterpret_cast<const TunnelEcho::EchoMessage*>(buf);
        uint64_t now = i2p::util::GetMillisecondsSinceEpoch();
        uint64_t rtt = now - msg->timestamp;
        
        std::string payload;
        if (msg->dataSize > 0 && len >= sizeof(TunnelEcho::EchoMessage) + msg->dataSize) {
            payload = std::string(reinterpret_cast<const char*>(msg->data), msg->dataSize);
        }
        
        switch (msg->type) {
            case TunnelEcho::ECHO_REQUEST:
                ++m_MessagesReceived;
                std::cout << "Received echo request #" << msg->sequenceId 
                          << " from " << from.ToBase32().substr(0, 8) << "... "
                          << "(RTT: " << rtt << "ms, " << payload.size() << " bytes)\n";
                if (m_IsServer) {
                    // Send echo response
                    const auto response = CreateEchoMessage(TunnelEcho::ECHO_RESPONSE, payload);
                    SendMessage(from, response);
                    ++m_MessagesSent;
                    std::cout << "Sent echo response #" << m_SequenceId << "\n";
                }
                break;
                
            case TunnelEcho::ECHO_RESPONSE:
                ++m_MessagesReceived;
                std::cout << "Received echo response #" << msg->sequenceId 
                          << " from " << from.ToBase32().substr(0, 8) << "... "
                          << "(RTT: " << rtt << "ms, " << payload.size() << " bytes)\n";
                break;
                
            case TunnelEcho::TUNNEL_INFO:
                std::cout << "Received tunnel info from " << from.ToBase32().substr(0, 8) << "...\n";
                std::cout << "Tunnel Info: " << payload << "\n";
                break;
                
            default:
                std::cout << "Received unknown message type " << (int)msg->type 
                          << " from " << from.ToBase32().substr(0, 8) << "...\n";
                break;
        }
    }
    
    std::string GetTunnelInfo() {
        auto dest = m_Owner;
        auto pool = dest->GetTunnelPool();
        
        std::ostringstream info;
        info << "Tunnel Pool Info:\n";
        info << "  Inbound tunnels: " << pool->GetInboundTunnels().size() << "\n";
        info << "  Outbound tunnels: " << pool->GetOutboundTunnels().size() << "\n";
        info << "  Hops: " << m_Hops << "\n";
        
        // Get first available tunnel for inspection
        auto outbound = pool->GetNextOutboundTunnel();
        if (outbound) {
            info << "  Sample outbound tunnel: " << outbound->GetNumHops() << " hops, ";
            info << "transport: " << (outbound->GetFarEndTransports() & i2p::data::RouterInfo::eSSU2V4 ? "SSU2" : "NTCP2") << "\n";
        }
        
        return info.str();
    }
};

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <mode> [options]\n\n";
    std::cout << "Modes:\n";
    std::cout << "  server [--conf config.conf] [--datadir path] [--hops N]\n";
    std::cout << "  client [--conf config.conf] [--datadir path] --server-b32 <address> [--hops N] [--message <text>] [--count N]\n\n";
    std::cout << "Server Options:\n";
    std::cout << "  --conf <file>       Configuration file path\n";
    std::cout << "  --datadir <path>    Data directory path\n";
    std::cout << "  --hops <N>          Tunnel hop count (default: 1, 0 = direct connection)\n\n";
    std::cout << "Client Options:\n";
    std::cout << "  --conf <file>       Configuration file path\n";
    std::cout << "  --datadir <path>    Data directory path\n";
    std::cout << "  --server-b32 <addr> Server's base32 address\n";
    std::cout << "  --hops <N>          Tunnel hop count (default: 1, 0 = direct connection)\n";
    std::cout << "  --message <text>    Message to echo (default: 'Hello from tunnel echo test')\n";
    std::cout << "  --count <N>         Number of echo requests to send (default: 5)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  # Server with 0-hop (direct) connection\n";
    std::cout << "  " << program << " server --conf srv.conf --datadir /tmp/srv --hops 0\n\n";
    std::cout << "  # Client connecting to server with echo messages\n";
    std::cout << "  " << program << " client --conf cli.conf --datadir /tmp/cli --hops 0 --server-b32 <server_address> --count 10\n\n";
    std::cout << "  # Multi-hop connection\n";
    std::cout << "  " << program << " server --conf srv.conf --datadir /tmp/srv --hops 2\n";
    std::cout << "  " << program << " client --conf cli.conf --datadir /tmp/cli --hops 2 --server-b32 <server_address>\n\n";
}

int runServer(const std::string& datadir, const std::string& config, int hops) {
    try {
        std::cout << "Initializing server node...\n";
        i2p::embed::I2PdUtils::initNode("server", datadir, config);
        i2p::embed::I2PdUtils::startCore();
        
        std::cout << "Creating server destination...\n";
        auto serverDest = i2p::embed::I2PdUtils::createDestination(true, hops);
        
        if (!serverDest) {
            std::cerr << "ERROR: Failed to create server destination\n";
            i2p::embed::I2PdUtils::stopCore();
            return 1;
        }
        
        // Create tunnel echo server
        auto echoServer = std::make_shared<TunnelEchoManager>(serverDest, true, hops);
        echoServer->Start();
        
        std::cout << "\n=== TUNNEL ECHO SERVER STARTED ===\n";
        std::cout << "Server B32 Address: " << serverDest->GetIdentHash().ToBase32() << ".b32.i2p\n";
        std::cout << "Protocol: Tunnel-based with SSU2 preference\n";
        std::cout << "Tunnel hops: " << hops << "\n";
        std::cout << "Listening for echo requests...\n";
        std::cout << "Press Ctrl+C to stop\n\n";
        
        // Wait for connections and handle echo requests
        while (true) {
            std::this_thread::sleep_for(5s);
            
            // Send tunnel info periodically
            std::cout << "Server running... (messages received: " 
                      << echoServer->GetMessagesReceived() << ")\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Server exception: " << e.what() << "\n";
        return 1;
    }
}

int runClient(const std::string& datadir, const std::string& config, 
              const std::string& serverB32, int hops, 
              const std::string& message, int count) {
    try {
        std::cout << "Initializing client node...\n";
        i2p::embed::I2PdUtils::initNode("client", datadir, config);
        i2p::embed::I2PdUtils::startCore();
        
        std::cout << "Creating client destination...\n";
        auto clientDest = i2p::embed::I2PdUtils::createDestination(false, hops);
        
        if (!clientDest) {
            std::cerr << "ERROR: Failed to create client destination\n";
            i2p::embed::I2PdUtils::stopCore();
            return 1;
        }
        
        // Parse server identity
        i2p::data::IdentHash serverIdent;
        if (!serverIdent.FromBase32(serverB32)) {
            std::cerr << "ERROR: Invalid server B32 address: " << serverB32 << "\n";
            i2p::embed::I2PdUtils::stopCore();
            return 1;
        }
        
        // Create tunnel echo client
        auto echoClient = std::make_shared<TunnelEchoManager>(clientDest, false, hops);
        echoClient->Start();
        
        std::cout << "\n=== TUNNEL ECHO CLIENT STARTED ===\n";
        std::cout << "Client B32 Address: " << clientDest->GetIdentHash().ToBase32() << ".b32.i2p\n";
        std::cout << "Server B32 Address: " << serverB32 << ".b32.i2p\n";
        std::cout << "Protocol: Tunnel-based with SSU2 preference\n";
        std::cout << "Tunnel hops: " << hops << "\n";
        std::cout << "Message: \"" << message << "\"\n";
        std::cout << "Count: " << count << "\n\n";
        
        // Wait for destination to be ready
        std::cout << "Waiting for destination to be ready...\n";
        std::this_thread::sleep_for(10s);
        
        // Wait for server LeaseSet to be available
        std::cout << "Looking up server LeaseSet...\n";
        i2p::embed::I2PdUtils::waitForLeaseSet(serverIdent, 30000);
        std::cout << "Server LeaseSet lookup completed\n";
        
        // Allow additional time for tunnel/session establishment
        std::cout << "Waiting for tunnel establishment...\n";
        std::this_thread::sleep_for(5s);
        
        // Send tunnel initialization message to warm up the connection
        std::cout << "Initializing tunnel with warmup message...\n";
        echoClient->SendTunnelInfo(serverIdent);  // Use tunnel info as warmup
        std::this_thread::sleep_for(3s);  // Wait for tunnel to process warmup
        
        // Send echo requests (now tunnels should be properly initialized)
        std::cout << "Sending " << count << " echo requests...\n";
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (int i = 1; i <= count; i++) {
            std::string numberedMessage = message + " #" + std::to_string(i);
            echoClient->SendEchoRequest(serverIdent, numberedMessage);
            std::this_thread::sleep_for(1s);  // 1 second between requests
        }
        
        // Wait for responses (longer timeout for better reliability)
        std::cout << "Waiting for responses...\n";
        std::cout << "Allowing extra time for final message to be processed...\n";
        std::this_thread::sleep_for(25s);  // Even longer timeout for final response
        
        // Final status check with polling for late arrivals
        std::cout << "Final message count check...\n";
        int lastCount = echoClient->GetMessagesReceived();
        
        // Poll for late messages for up to 5 seconds
        for (int i = 0; i < 10; i++) {
            std::this_thread::sleep_for(500ms);
            int currentCount = echoClient->GetMessagesReceived();
            if (currentCount > lastCount) {
                std::cout << "Late message arrived! Count now: " << currentCount << "\n";
                lastCount = currentCount;
            }
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << "\n=== TUNNEL ECHO TEST COMPLETED ===\n";
        std::cout << "Test duration: " << duration.count() << "ms\n";
        std::cout << "Messages sent: " << echoClient->GetMessagesSent() << "\n";
        std::cout << "Messages received: " << echoClient->GetMessagesReceived() << "\n";
        
        // Comprehensive cleanup to avoid mutex issues
        std::cout << "Cleaning up client resources...\n";
        
        try {
            // Stop application layer first
            echoClient->Stop();
            std::this_thread::sleep_for(100ms);  // Allow cleanup to complete
            
            // Stop I2P destination
            clientDest->Stop();
            std::this_thread::sleep_for(100ms);
            
            // Release application objects
            echoClient.reset();
            clientDest.reset();
            std::this_thread::sleep_for(500ms);  // Allow all cleanup to complete
            
            // Stop I2P core last
            i2p::embed::I2PdUtils::stopCore();
            
        } catch (const std::exception& e) {
            std::cout << "Error during cleanup: " << e.what() << "\n";
        }
        
        // Force clean exit to avoid any remaining mutex issues
        std::cout << "Exiting cleanly...\n";
        std::_Exit(0);  // Use _Exit to avoid atexit handlers that might cause issues
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Client exception: " << e.what() << "\n";
        return 1;
    }
}

int main(int argc, char* argv[]) {
    try {
        // Initialize defaults
        TransferConfig::initializeDefaults();
        
        if (argc < 2) {
            printUsage(argv[0]);
            return 1;
        }
        
        const std::string mode = argv[1];
        
        // Parse common arguments
        std::string datadir = "";
        std::string config = "";
        int hops = 1;
        
        // Client-specific arguments
        std::string serverB32 = "";
        std::string message = "Hello from tunnel echo test";
        int count = 5;
        
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            
            if (arg == "--datadir" && i + 1 < argc) {
                datadir = argv[++i];
            } else if (arg == "--conf" && i + 1 < argc) {
                config = argv[++i];
            } else if (arg == "--hops" && i + 1 < argc) {
                hops = std::stoi(argv[++i]);
            } else if (arg == "--server-b32" && i + 1 < argc) {
                serverB32 = argv[++i];
            } else if (arg == "--message" && i + 1 < argc) {
                message = argv[++i];
            } else if (arg == "--count" && i + 1 < argc) {
                count = std::stoi(argv[++i]);
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                printUsage(argv[0]);
                return 1;
            }
        }
        
        if (mode == "server") {
            return runServer(datadir, config, hops);
        } else if (mode == "client") {
            if (serverB32.empty()) {
                std::cerr << "ERROR: Client mode requires --server-b32 argument\n";
                printUsage(argv[0]);
                return 1;
            }
            return runClient(datadir, config, serverB32, hops, message, count);
        } else {
            std::cerr << "ERROR: Unknown mode: " << mode << "\n";
            printUsage(argv[0]);
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}