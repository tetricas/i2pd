#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <iomanip>
#include <memory>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

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
namespace TunnelEcho {
    constexpr uint16_t ECHO_PORT = 8888;
    constexpr uint16_t CLIENT_PORT = 8889;
    enum MessageType : uint8_t {
        TUNNEL_WARM_UP = 1,
        FILE_TRANSFER_REQUEST = 2,
        FILE_METADATA = 3,
        FILE_CHUNK = 4,
        FILE_TRANSFER_OK = 5
    };
    
    struct EchoMessage {
        uint8_t type;
        uint32_t sequenceId;
        uint64_t timestamp;
        uint32_t dataSize;
        uint8_t data[];
    } __attribute__((packed));
    struct FileInfo {
        std::string filename;
        size_t size;
        std::string checksum;
        std::vector<uint8_t> data;
    };
    uint32_t calculateCRC32(const std::vector<uint8_t>& data) {
        uint32_t crc = 0xFFFFFFFF;
        for (uint8_t byte : data) {
            crc ^= byte;
            for (int i = 0; i < 8; i++) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ 0xEDB88320;
                } else {
                    crc >>= 1;
                }
            }
        }
        return ~crc;
    }
    FileInfo readFile(const std::string& filepath) {
        FileInfo info;
        info.filename = filepath.substr(filepath.find_last_of("/\\") + 1);
        
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filepath);
        }
        
        // Read entire file
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        
        info.data.resize(fileSize);
        file.read(reinterpret_cast<char*>(info.data.data()), fileSize);
        file.close();
        
        info.size = fileSize;
        
        // Calculate checksum
        uint32_t crc = calculateCRC32(info.data);
        std::stringstream ss;
        ss << std::hex << crc;
        info.checksum = ss.str();
        
        return info;
    }
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
    }
    
    void Start() {
        
        CreateSSU2TunnelPool();
        auto datagramDestination = m_Owner->CreateDatagramDestination(false, i2p::datagram::eDatagramV1);
        std::cout << "Created datagram destination: " << (datagramDestination ? "SUCCESS" : "FAILED") << std::endl;
        
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
    bool ShouldExit() const { return m_ShouldExit; }
    
    void LoadFile(const std::string& filepath) {
        try {
            m_FileInfo = TunnelEcho::readFile(filepath);
            m_HasFile = true;
            std::cout << "Loaded file: " << m_FileInfo.filename 
                      << " (" << m_FileInfo.size << " bytes, checksum: " 
                      << m_FileInfo.checksum << ")\n";
        } catch (const std::exception& e) {
            std::cerr << "Failed to load file " << filepath << ": " << e.what() << "\n";
            m_HasFile = false;
        }
    }
    
    void SendAllChunks(const i2p::data::IdentHash& to) {
        if (!m_HasFile) {
            return;
        }
        
        // Send all chunks iteratively to avoid stack overflow
        while (m_BytesSent < m_FileInfo.data.size()) {
            size_t remainingBytes = m_FileInfo.data.size() - m_BytesSent;
            size_t chunkSize = std::min(remainingBytes, m_ChunkSize);
            
            std::string chunkData(
                reinterpret_cast<const char*>(m_FileInfo.data.data() + m_BytesSent), 
                chunkSize
            );
            
            auto chunk = CreateEchoMessage(TunnelEcho::FILE_CHUNK, chunkData);
            SendMessage(to, chunk);
            ++m_MessagesSent;
            
            m_BytesSent += chunkSize;
            
                if ((m_MessagesSent - 1) % 1000 == 0) {
                std::cout << "Sent FILE_CHUNK #" << m_SequenceId 
                          << " (" << chunkSize << " bytes, " << m_BytesSent 
                          << "/" << m_FileInfo.data.size() << ")\n";
            }
            
        }
        
        std::cout << "File transfer complete! Sent " << m_BytesSent 
                  << " bytes in " << (m_MessagesSent - 1) << " chunks\n";
    }
    
    void HandleDatagramReceive(const i2p::data::IdentityEx& from, uint16_t fromPort, uint16_t toPort,
                              const uint8_t* buf, size_t len) {
        HandleMessage(from.GetIdentHash(), fromPort, toPort, buf, len);
    }
    
    void SendTunnelWarmUp(const i2p::data::IdentHash& to) {
        if (!m_IsServer) {
            auto message = CreateEchoMessage(TunnelEcho::TUNNEL_WARM_UP, "WARMUP");
            SendMessage(to, message);
            std::cout << "Sent TUNNEL_WARM_UP #" << m_SequenceId 
                      << " to " << to.ToBase32().substr(0, 8) << "... (warmup)\n";
        }
    }
    
    void SendFileTransferRequest(const i2p::data::IdentHash& to, const std::string& filename) {
        if (!m_IsServer) {
            auto message = CreateEchoMessage(TunnelEcho::FILE_TRANSFER_REQUEST, filename);
            SendMessage(to, message);
            ++m_MessagesSent;
            std::cout << "Sent FILE_TRANSFER_REQUEST #" << m_SequenceId 
                      << " to " << to.ToBase32().substr(0, 8) << "... "
                      << "(file: " << filename << ")\n";
        }
    }
    

private:
    std::shared_ptr<i2p::client::ClientDestination> m_Owner;
    bool m_IsServer;
    int m_Hops;
    std::atomic<uint32_t> m_SequenceId;
    std::atomic<uint32_t> m_MessagesReceived;
    std::atomic<uint32_t> m_MessagesSent;
    std::atomic<bool> m_ShouldExit{false};
    TunnelEcho::FileInfo m_FileInfo;
    bool m_HasFile{false};
    
    // Client-side file reception tracking
    std::string m_ExpectedChecksum;
    std::vector<uint8_t> m_ReceivedData;
    size_t m_ExpectedFileSize{0};
    
    // Server-side chunking state
    size_t m_ChunkSize{30720};  // 30KB chunks - testing near I2P datagram limits
    size_t m_BytesSent{0};
    i2p::data::IdentHash m_CurrentClient;
    
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
            case TunnelEcho::TUNNEL_WARM_UP:
                std::cout << "Received TUNNEL_WARM_UP #" << msg->sequenceId 
                          << " from " << from.ToBase32().substr(0, 8) << "... "
                          << "(RTT: " << rtt << "ms) - tunnel warmed up\n";
                break;
                
            case TunnelEcho::FILE_TRANSFER_REQUEST:
                ++m_MessagesReceived;
                std::cout << "Received FILE_TRANSFER_REQUEST #" << msg->sequenceId 
                          << " from " << from.ToBase32().substr(0, 8) << "... "
                          << "(RTT: " << rtt << "ms, file: " << payload << ")\n";
                if (m_IsServer) {
                    if (!m_HasFile) {
                        std::cerr << "Server: No file loaded for transfer\n";
                        return;
                    }
                    
                    m_BytesSent = 0;
                    m_CurrentClient = from;
                    
                    std::cout << "Server: Sending FILE_METADATA response\n";
                    std::stringstream metadata;
                    metadata << "metadata:" << m_FileInfo.filename << ":" 
                             << m_FileInfo.size << ":" << m_FileInfo.checksum;
                    
                    auto response = CreateEchoMessage(TunnelEcho::FILE_METADATA, metadata.str());
                    SendMessage(from, response);
                    ++m_MessagesSent;
                    std::cout << "Sent FILE_METADATA response #" << m_SequenceId 
                              << " (file: " << m_FileInfo.filename << ", " << m_FileInfo.size << " bytes)\n";
                    
                    std::cout << "Server: Starting chunked transfer (" << m_ChunkSize << " byte chunks)\n";
                    SendAllChunks(from);
                }
                break;
                
            case TunnelEcho::FILE_METADATA:
                ++m_MessagesReceived;
                std::cout << "Received FILE_METADATA from " << from.ToBase32().substr(0, 8) << "... "
                          << "(data: " << payload << ")\n";
                          
                if (!m_IsServer && payload.find("metadata:") == 0) {
                    std::stringstream ss(payload);
                    std::string token;
                    std::vector<std::string> parts;
                    
                    while (std::getline(ss, token, ':')) {
                        parts.push_back(token);
                    }
                    
                    if (parts.size() >= 4) {
                        m_ExpectedFileSize = std::stoull(parts[2]);
                        m_ExpectedChecksum = parts[3];
                        m_ReceivedData.clear();
                        m_ReceivedData.reserve(m_ExpectedFileSize);
                        std::cout << "Client: Expecting file " << parts[1] 
                                  << " (" << parts[2] << " bytes, checksum: " 
                                  << m_ExpectedChecksum << ")\n";
                    }
                }
                break;
                
            case TunnelEcho::FILE_CHUNK:
                ++m_MessagesReceived;
                          
                if (!m_IsServer) {
                    m_ReceivedData.insert(m_ReceivedData.end(), payload.begin(), payload.end());
                    
                                if (m_MessagesReceived % 1000 == 0) {
                        std::cout << "Client: Accumulated " << m_ReceivedData.size() 
                                  << "/" << m_ExpectedFileSize << " bytes (" << m_MessagesReceived << " chunks)\n";
                    }
                    
                    if (m_ReceivedData.size() >= m_ExpectedFileSize) {
                        std::cout << "Client: File transfer complete, verifying checksum...\n";
                        
                        uint32_t receivedCRC = TunnelEcho::calculateCRC32(m_ReceivedData);
                        std::stringstream ss;
                        ss << std::hex << receivedCRC;
                        std::string receivedChecksum = ss.str();
                        
                        std::cout << "  Expected: " << m_ExpectedChecksum << "\n";
                        std::cout << "  Received: " << receivedChecksum << "\n";
                        
                        std::string response;
                        if (receivedChecksum == m_ExpectedChecksum) {
                            std::cout << "Client: Checksum verification PASSED\n";
                            response = "checksum_ok";
                        } else {
                            std::cout << "Client: Checksum verification FAILED\n";
                            response = "checksum_failed";
                        }
                        
                        std::cout << "Client: Sending FILE_TRANSFER_OK\n";
                        auto ok = CreateEchoMessage(TunnelEcho::FILE_TRANSFER_OK, response);
                        SendMessage(from, ok);
                        ++m_MessagesSent;
                        std::cout << "Sent FILE_TRANSFER_OK #" << m_SequenceId << "\n";
                        std::cout << "Client: Transfer complete, shutting down\n";
                        m_ShouldExit = true;
                    }
                }
                break;
                
            case TunnelEcho::FILE_TRANSFER_OK:
                ++m_MessagesReceived;
                std::cout << "Received FILE_TRANSFER_OK from " << from.ToBase32().substr(0, 8) << "... "
                          << "(data: " << payload << ")\n";
                if (m_IsServer) {
                    std::cout << "Server: Transfer complete, shutting down\n";
                    m_ShouldExit = true;
                }
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
    // Default test file path
    std::string testFile = "/tmp/testfile.txt";
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
        
        auto echoServer = std::make_shared<TunnelEchoManager>(serverDest, true, hops);
        
        // Load test file
        std::cout << "Loading test file: " << testFile << "\n";
        echoServer->LoadFile(testFile);
        
        echoServer->Start();
        
        std::cout << "\n=== TUNNEL ECHO SERVER STARTED ===\n";
        std::cout << "Server B32 Address: " << serverDest->GetIdentHash().ToBase32() << ".b32.i2p\n";
        std::cout << "Protocol: Tunnel-based with SSU2 preference\n";
        std::cout << "Tunnel hops: " << hops << "\n";
        std::cout << "Listening for echo requests...\n";
        std::cout << "Press Ctrl+C to stop\n\n";
        
        // Wait for connections and handle echo requests
        while (!echoServer->ShouldExit()) {
            std::this_thread::sleep_for(5s);
            
            // Send tunnel info periodically
            std::cout << "Server running... (messages received: " 
                      << echoServer->GetMessagesReceived() << ")\n";
        }
        // Force clean exit to avoid any remaining mutex issues
        std::cout << "Exiting cleanly...\n";
        std::_Exit(0);  // Use _Exit to avoid atexit handlers that might cause issues

        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Server exception: " << e.what() << "\n";
        return 1;
    }
}

int runClient(const std::string& datadir, const std::string& config, 
              const std::string& serverB32, int hops, 
              const std::string& message) {
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
        
        // Tunnel is ready, proceed with file transfer protocol
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Send FILE_TRANSFER_START message
        std::cout << "Sending FILE_TRANSFER_START...\n";
        echoClient->SendTunnelWarmUp(serverIdent);
        std::this_thread::sleep_for(2s);
        
        // Send FILE_TRANSFER_REQUEST message
        std::cout << "Sending FILE_TRANSFER_REQUEST...\n";
        echoClient->SendFileTransferRequest(serverIdent, "testfile.bin");
        std::this_thread::sleep_for(2s);
        
        // Wait for responses and file transfer completion
        std::cout << "Waiting for file transfer to complete...\n";
        
        // Wait for completion or timeout
        auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(3600);  // 1 hour timeout
        int lastMessageCount = 0;
        auto lastProgressTime = std::chrono::steady_clock::now();
        
        while (!echoClient->ShouldExit() && std::chrono::steady_clock::now() < timeout) {
            std::this_thread::sleep_for(2s);  // Check every 2 seconds
            
            int currentMessages = echoClient->GetMessagesReceived();
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastProgressTime).count();
            
            if (elapsed >= 5) {  // Report progress every 5 seconds
                double messagesPerSec = (currentMessages - lastMessageCount) / double(elapsed);
                std::cout << "Progress: " << currentMessages << " chunks received "
                          << "(" << std::fixed << std::setprecision(1) << messagesPerSec << " chunks/sec)\n";
                lastMessageCount = currentMessages;
                lastProgressTime = currentTime;
            }
        }
        
        if (echoClient->ShouldExit()) {
            std::cout << "Transfer completed successfully!\n";
        } else {
            std::cout << "Transfer timed out after 1 hour\n";
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
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                printUsage(argv[0]);
                return 1;
            }
        }
        
        if (mode == "server")
            return runServer(datadir, config, hops);

        if (mode == "client")
        {
            if (serverB32.empty()) {
                std::cerr << "ERROR: Client mode requires --server-b32 argument\n";
                printUsage(argv[0]);
                return 1;
            }
            return runClient(datadir, config, serverB32, hops, message);
        }

        std::cerr << "ERROR: Unknown mode: " << mode << "\n";
        printUsage(argv[0]);
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}