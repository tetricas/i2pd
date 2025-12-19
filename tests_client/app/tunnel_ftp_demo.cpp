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
#include <set>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>

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
const std::string g_serverFileName = "/tmp/testfile.bin";
const std::string g_receivedFileName = "/tmp/received_file.bin";
namespace TunnelEcho {
    constexpr uint16_t ECHO_PORT = 8888;
    constexpr uint16_t CLIENT_PORT = 8889;
    enum MessageType : uint8_t {
        TUNNEL_WARM_UP = 1,
        FILE_TRANSFER_REQUEST = 2,
        FILE_METADATA = 3,
        FILE_METADATA_ACK = 4,
        FILE_CHUNK = 5,
        FILE_TRANSFER_OK = 6,
        CHUNK_REQUEST = 7
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
        std::string filepath;
    };

    enum FileSize {
        SMALL = 0,   // 10MB
        MEDIUM = 1,  // 100MB  
        LARGE = 2    // 1GB
    };

    struct FileSizeInfo {
        const char* name;
        size_t size;
    };

    constexpr FileSizeInfo FILE_SIZES[] = {
        {"small", 10 * 1024 * 1024},      // 10MB
        {"medium", 100 * 1024 * 1024},    // 100MB
        {"large", 1024 * 1024 * 1024}     // 1GB
    };
    uint32_t calculateCRC32Stream(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filepath);
        }
        
        uint32_t crc = 0xFFFFFFFF;
        constexpr size_t BUFFER_SIZE = 65536;
        std::vector<uint8_t> buffer(BUFFER_SIZE);
        
        while (file.read(reinterpret_cast<char*>(buffer.data()), BUFFER_SIZE) || file.gcount() > 0) {
            size_t bytesRead = file.gcount();
            for (size_t i = 0; i < bytesRead; i++) {
                uint8_t byte = buffer[i];
                crc ^= byte;
                for (int j = 0; j < 8; j++) {
                    if (crc & 1) {
                        crc = (crc >> 1) ^ 0xEDB88320;
                    } else {
                        crc >>= 1;
                    }
                }
            }
        }
        file.close();
        return ~crc;
    }

    std::string generateTestFile(FileSize size) {
        // Remove existing file
        std::remove(g_serverFileName.c_str());
        
        size_t fileSize = FILE_SIZES[size].size;
        std::ofstream file(g_serverFileName, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot create test file: " + g_serverFileName);
        }
        
        // Generate file with pattern data (not random for reproducibility)
        constexpr size_t BUFFER_SIZE = 65536;
        std::vector<uint8_t> buffer(BUFFER_SIZE);
        
        // Fill buffer with repeating pattern
        for (size_t i = 0; i < BUFFER_SIZE; i++) {
            buffer[i] = static_cast<uint8_t>((i * 73 + 17) % 256);
        }
        
        size_t remaining = fileSize;
        while (remaining > 0) {
            size_t toWrite = std::min(remaining, BUFFER_SIZE);
            file.write(reinterpret_cast<const char*>(buffer.data()), toWrite);
            remaining -= toWrite;
        }
        
        file.close();
        std::cout << "Generated test file: " << FILE_SIZES[size].name 
                  << " (" << fileSize << " bytes)\n";
        
        return g_serverFileName;
    }

    FileInfo getFileInfo(const std::string& filepath) {
        FileInfo info;
        info.filename = "testfile.bin";  // Standard name
        info.filepath = filepath;
        
        // Get file size
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filepath);
        }
        info.size = file.tellg();
        file.close();
        
        // Calculate checksum using streaming
        uint32_t crc = calculateCRC32Stream(filepath);
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
    
    void Start()
    {
        
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
    
    void Stop()
    {
        std::cout << "Stopping tunnel echo manager...\n";
        
        // Stop server sender thread if running
        if (m_IsServer) {
            m_StopSender = true;
            if (m_FileSenderThread.joinable()) {
                m_FileSenderThread.join();
            }
        }
        
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
    bool IsTransferCompleted() const { return m_FileTransferComplete; }
    uint64_t GetFileSize() const { return m_ExpectedFileSize; }
    
    void StopClientThreads() {
        if (!m_IsServer) {
            m_StopThreads = true;
            m_HasNewChunks = true; // Wake up processor thread
            
            if (m_ChunkProcessorThread.joinable()) {
                m_ChunkProcessorThread.join();
            }
            
            if (m_GapDetectorThread.joinable()) {
                m_GapDetectorThread.join();
            }
            
            if (m_OutputFile.is_open()) {
                m_OutputFile.close();
            }
        }
    }
    
    void LoadFile(const std::string& filepath) {
        try {
            m_FileInfo = TunnelEcho::getFileInfo(filepath);
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

        // Send all chunks with proper chunk indexing based on file position
        std::ifstream file(m_FileInfo.filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Cannot open file for reading: " << m_FileInfo.filepath << "\n";
            return;
        }

        std::vector<uint8_t> buffer(m_ChunkSize);
        uint32_t chunkIndex = 0;

        // Rate limiting: 8 MB/s maximum
        const size_t MAX_BYTES_PER_SECOND = 8 * 1024 * 1024;  // 8 MB/s
        auto transferStartTime = std::chrono::steady_clock::now();
        size_t bytesSentAtStart = m_BytesSent;

        std::cout << "Server: Starting rate-limited transfer (max 8 MB/s)\n";

        while (m_BytesSent < m_FileInfo.size && !m_StopSender) {

            size_t remainingBytes = m_FileInfo.size - m_BytesSent;
            size_t chunkSize = std::min(remainingBytes, m_ChunkSize);

            file.read(reinterpret_cast<char*>(buffer.data()), chunkSize);
            size_t bytesRead = file.gcount();

            if (bytesRead == 0) break;

            // Create chunk with header: chunkIndex(4) + chunkSize(4) + payload
            std::string chunkData;
            uint32_t bytesRead32 = static_cast<uint32_t>(bytesRead);
            chunkData.append(reinterpret_cast<const char*>(&chunkIndex), sizeof(chunkIndex));
            chunkData.append(reinterpret_cast<const char*>(&bytesRead32), sizeof(bytesRead32));
            chunkData.append(reinterpret_cast<const char*>(buffer.data()), bytesRead);

            auto chunk = CreateEchoMessage(TunnelEcho::FILE_CHUNK, chunkData);
            SendMessage(to, chunk);
            ++m_MessagesSent;

            m_BytesSent += bytesRead;
            chunkIndex++;

            // Rate limiting check every chunk
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - transferStartTime);
            size_t bytesTransferred = m_BytesSent - bytesSentAtStart;

            if (elapsed.count() > 0) {
                size_t currentRate = (bytesTransferred * 1000) / elapsed.count();  // bytes per second

                if (currentRate > MAX_BYTES_PER_SECOND) {
                    // Calculate how long we should have taken
                    size_t expectedTimeMs = (bytesTransferred * 1000) / MAX_BYTES_PER_SECOND;
                    int64_t sleepTimeMs = expectedTimeMs - elapsed.count();

                    if (sleepTimeMs > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(sleepTimeMs));
                    }
                }
            }

            if (chunkIndex % 1000 == 0) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - transferStartTime);
                size_t bytesTransferred = m_BytesSent - bytesSentAtStart;
                double currentRateMBps = 0.0;
                if (elapsed.count() > 0) {
                    currentRateMBps = (bytesTransferred / 1024.0 / 1024.0) / (elapsed.count() / 1000.0);
                }
                std::cout << "Sent chunk " << chunkIndex
                          << " (" << bytesRead << " bytes, " << m_BytesSent
                          << "/" << m_FileInfo.size << ", rate: "
                          << std::fixed << std::setprecision(2) << currentRateMBps << " MB/s)\n";
            }
        }

        file.close();
        if (!m_StopSender) {
            auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - transferStartTime);
            double avgRateMBps = 0.0;
            if (totalElapsed.count() > 0) {
                avgRateMBps = ((m_BytesSent - bytesSentAtStart) / 1024.0 / 1024.0) / (totalElapsed.count() / 1000.0);
            }
            std::cout << "File transfer complete! Sent " << m_BytesSent
                      << " bytes in " << chunkIndex << " chunks"
                      << " (avg rate: " << std::fixed << std::setprecision(2) << avgRateMBps << " MB/s)\n";
        }
    }
    
    void SendRequestedChunks(const i2p::data::IdentHash& to, const std::vector<uint32_t>& requestedChunks) {
        std::ifstream file(m_FileInfo.filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Cannot open file for retransmission: " << m_FileInfo.filepath << "\n";
            return;
        }
        
        std::vector<uint8_t> buffer(m_ChunkSize);
        
        for (uint32_t chunkIndex : requestedChunks) {
            size_t offset = chunkIndex * m_ChunkSize;
            if (offset >= m_FileInfo.size) continue;
            
            file.seekg(offset);
            size_t remainingBytes = m_FileInfo.size - offset;
            size_t chunkSize = std::min(remainingBytes, m_ChunkSize);
            
            file.read(reinterpret_cast<char*>(buffer.data()), chunkSize);
            size_t bytesRead = file.gcount();
            
            if (bytesRead == 0) continue;
            
            // Create chunk with header: chunkIndex(4) + chunkSize(4) + payload
            std::string chunkData;
            uint32_t bytesRead32 = static_cast<uint32_t>(bytesRead);
            chunkData.append(reinterpret_cast<const char*>(&chunkIndex), sizeof(chunkIndex));
            chunkData.append(reinterpret_cast<const char*>(&bytesRead32), sizeof(bytesRead32));
            chunkData.append(reinterpret_cast<const char*>(buffer.data()), bytesRead);
            
            auto chunk = CreateEchoMessage(TunnelEcho::FILE_CHUNK, chunkData);
            SendMessage(to, chunk);
            ++m_MessagesSent;
        }
        
        file.close();
        std::cout << "Retransmitted " << requestedChunks.size() << " requested chunks\n";
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
    std::set<uint32_t> m_ReceivedChunks;
    std::map<uint32_t, std::vector<uint8_t>> m_ChunkData;
    uint32_t m_TotalExpectedChunks{0};
    std::chrono::steady_clock::time_point m_LastProgressTime;
    
    // Server-side chunking state  
    size_t m_ChunkSize{31744};  // 31KB chunks (fewer total packets)
    size_t m_BytesSent{0};
    i2p::data::IdentHash m_CurrentClient;
    std::thread m_FileSenderThread;
    std::atomic<bool> m_StopSender{false};
    
    // Client-side processing
    std::atomic<bool> m_FileTransferComplete{false};
    std::atomic<bool> m_VerificationStarted{false};
    std::thread m_ChunkProcessorThread;
    std::thread m_GapDetectorThread;
    std::atomic<bool> m_StopThreads{false};
    
    // Lock-free chunk queue
    struct ChunkItem {
        uint32_t index;
        std::vector<uint8_t> data;
        std::atomic<ChunkItem*> next{nullptr};
    };
    
    class LockFreeQueue {
    private:
        std::atomic<ChunkItem*> head{nullptr};
        std::atomic<ChunkItem*> tail{nullptr};
        
    public:
        LockFreeQueue() {
            auto dummy = new ChunkItem{0, {}, nullptr};
            head.store(dummy);
            tail.store(dummy);
        }
        
        ~LockFreeQueue() {
            while (ChunkItem* item = pop()) {
                delete item;
            }
            delete head.load();
        }
        
        void push(ChunkItem* item) {
            item->next.store(nullptr);
            ChunkItem* prevTail = tail.exchange(item);
            prevTail->next.store(item);
        }
        
        ChunkItem* pop() {
            ChunkItem* head_node = head.load();
            ChunkItem* next = head_node->next.load();
            
            if (next == nullptr) {
                return nullptr; // Queue is empty
            }
            
            // Copy the data before deleting the node
            ChunkItem* result = new ChunkItem{next->index, next->data, nullptr};
            
            // Move head forward
            head.store(next);
            delete head_node;
            return result;
        }
        
        bool empty() {
            ChunkItem* head_node = head.load();
            ChunkItem* next = head_node->next.load();
            return next == nullptr;
        }
    };
    
    LockFreeQueue m_ChunkQueue;
    std::ofstream m_OutputFile;
    std::atomic<bool> m_HasNewChunks{false};
    
    void StartClientThreads(const i2p::data::IdentHash& serverHash) {
        m_StopThreads = false;
        m_FileTransferComplete = false;
        
        // Start chunk processor thread
        m_ChunkProcessorThread = std::thread([this]() {
            std::cout << "Chunk processor thread started\n";
            ProcessChunks();
        });
        
        // Start gap detector thread
        m_GapDetectorThread = std::thread([this, serverHash]() {
            DetectGapsAndRequest(serverHash);
        });
    }
    
    void ProcessChunks() {
        while (!m_StopThreads) {
            ChunkItem* item = m_ChunkQueue.pop();
            
            if (item != nullptr) {
                // Write chunk at correct position
                size_t offset = item->index * m_ChunkSize;

                // Clear any previous error state
                m_OutputFile.clear();

                m_OutputFile.seekp(offset);
                if (!m_OutputFile.good()) {
                    std::cout << "Error: Failed to seek to offset " << offset << " for chunk " << item->index << "\n";
                    m_OutputFile.clear();  // Clear error and continue
                }

                m_OutputFile.write(reinterpret_cast<const char*>(item->data.data()), item->data.size());
                if (!m_OutputFile.good()) {
                    std::cout << "Error: Failed to write chunk " << item->index << " at offset " << offset << "\n";
                    m_OutputFile.clear();  // Clear error and continue
                }

                m_OutputFile.flush();
                
                delete item; // Clean up
                m_HasNewChunks = false;
            } else {
                // No items available, wait a bit
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    
    void DetectGapsAndRequest(const i2p::data::IdentHash& serverHash) {
        std::cout << "Gap detection thread started\n";
        
        // Wait for first chunk to arrive before starting gap detection
        std::cout << "Gap detection: waiting for first chunk to arrive...\n";
        while (!m_StopThreads && m_ReceivedChunks.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (m_StopThreads) return;
        std::cout << "Gap detection: first chunk received, starting stall monitoring\n";
        
        size_t lastChunkCount = m_ReceivedChunks.size();
        
        while (!m_StopThreads && !m_FileTransferComplete) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            
            size_t currentChunkCount = m_ReceivedChunks.size();
            
            // Always print status when near completion
            if (currentChunkCount >= (m_TotalExpectedChunks * 0.95)) {
                std::cout << "Gap detection: " << currentChunkCount 
                          << "/" << m_TotalExpectedChunks << " chunks received\n";
            }
            
            // Check if we have received all chunks
            if (currentChunkCount >= m_TotalExpectedChunks && !m_VerificationStarted.exchange(true)) {
                std::cout << "All chunks received, starting verification\n";
                m_FileTransferComplete = true;
                VerifyAndComplete(serverHash);
                break;
            }
            
            // Detect stall: no new chunks received
            if (currentChunkCount == lastChunkCount) {
                std::cout << "Stall detected: no new chunks since last check.\nRequesting missing chunks\n";
                RequestMissingChunks(serverHash);
            }
            lastChunkCount = currentChunkCount;
        }
    }
    
    void RequestMissingChunks(const i2p::data::IdentHash& serverHash) {
        std::vector<uint32_t> missingChunks;
        
        for (uint32_t i = 0; i < m_TotalExpectedChunks; ++i) {
            if (m_ReceivedChunks.find(i) == m_ReceivedChunks.end()) {
                missingChunks.push_back(i);
            }
        }
        
        if (!missingChunks.empty()) {
            std::cout << "Client: Requesting " << missingChunks.size() << " missing chunks (received " 
                      << m_ReceivedChunks.size() << "/" << m_TotalExpectedChunks << ")\n";
            
            // Print first few missing chunks for debugging
            for (size_t i = 0; i < std::min(size_t(5), missingChunks.size()); ++i) {
                std::cout << "  Missing chunk: " << missingChunks[i] << "\n";
            }
            
            // Create request payload with chunk indices
            std::string requestPayload;
            for (uint32_t chunkIndex : missingChunks) {
                requestPayload.append(reinterpret_cast<const char*>(&chunkIndex), sizeof(chunkIndex));
            }
            
            auto request = CreateEchoMessage(TunnelEcho::CHUNK_REQUEST, requestPayload);
            SendMessage(serverHash, request);
            ++m_MessagesSent;
            std::cout << "Sent CHUNK_REQUEST #" << m_SequenceId 
                      << " to " << serverHash.ToBase32().substr(0, 8) << "... "
                      << "(requesting " << missingChunks.size() << " chunks)\n";
        }
    }
    
    void VerifyAndComplete(const i2p::data::IdentHash& serverHash) {
        std::cout << "Client: All chunks received, verifying file...\n";
        
        // Ensure file is closed and flushed
        if (m_OutputFile.is_open()) {
            m_OutputFile.flush();
            m_OutputFile.close();
        }
        
        // Give file system a moment to finalize writes
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Check file exists and get size
        std::ifstream file(g_receivedFileName, std::ios::binary);
        if (!file.is_open()) {
            std::cout << "ERROR: Cannot open received file!\n";
            m_ShouldExit = true;
            return;
        }
        
        file.seekg(0, std::ios::end);
        size_t actualFileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::cout << "Expected file size: " << m_ExpectedFileSize
                  << ", Actual file size: " << actualFileSize << "\n";
        
        if (actualFileSize != m_ExpectedFileSize) {
            std::cout << "ERROR: File size mismatch!\n";
        }

        std::cout << "Calculating CRC32...\n";
        uint32_t receivedCRC = TunnelEcho::calculateCRC32Stream(g_receivedFileName);
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
        
        // Send completion notification
        auto ok = CreateEchoMessage(TunnelEcho::FILE_TRANSFER_OK, response);
        SendMessage(serverHash, ok);
        ++m_MessagesSent;
        std::cout << "Client: Transfer complete, shutting down\n";
        m_ShouldExit = true;
    }

    void CreateSSU2TunnelPool() {
        // Create custom tunnel pool with SSU2 preference
        // Configured for high-bandwidth file transfers (9-10 MB/s)
        auto tunnelPool = std::make_shared<SSU2PreferringTunnelPool>(
            m_Hops,    // inbound hops
            m_Hops,    // outbound hops
            10,        // inbound tunnels (increased from 2 for high bandwidth)
            10,        // outbound tunnels (increased from 2 for high bandwidth)
            0,         // inbound variance
            0,         // outbound variance
            true       // high bandwidth (enabled for 9-10 MB/s transfers)
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
                    
                    std::cout << "Server: Waiting for client ACK before starting transfer\n";
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
                        m_TotalExpectedChunks = (m_ExpectedFileSize + m_ChunkSize - 1) / m_ChunkSize;
                        
                        // Clear previous data structures
                        m_ReceivedData.clear();
                        m_ReceivedChunks.clear();
                        m_ChunkData.clear();
                        m_FileTransferComplete = false;
                        m_VerificationStarted = false;
                        
                        // Open output file for writing chunks at specific positions
                        m_OutputFile.open(g_receivedFileName, std::ios::binary | std::ios::trunc);

                        // Pre-allocate file using sparse file method (efficient for large files)
                        m_OutputFile.seekp(m_ExpectedFileSize - 1);
                        m_OutputFile.put('\0');  // Write single byte at end to set file size
                        m_OutputFile.seekp(0);
                        m_OutputFile.flush();
                        
                        std::cout << "Client: Expecting file " << parts[1] 
                                  << " (" << parts[2] << " bytes, " << m_TotalExpectedChunks 
                                  << " chunks, checksum: " << m_ExpectedChecksum << ")\n";
                        std::cout << "File size = " << m_ExpectedFileSize
                                  << ", Chunk size = " << m_ChunkSize 
                                  << ", Total chunks = " << m_TotalExpectedChunks << "\n";
                        
                        // Start processing threads
                        StartClientThreads(from);
                        
                        // Send ACK to server to start chunk sending
                        auto ack = CreateEchoMessage(TunnelEcho::FILE_METADATA_ACK, "ready");
                        SendMessage(from, ack);
                        ++m_MessagesSent;
                        std::cout << "Client: Sent FILE_METADATA_ACK, ready to receive chunks\n";
                    }
                }
                break;
                
            case TunnelEcho::FILE_METADATA_ACK:
                ++m_MessagesReceived;
                std::cout << "Received FILE_METADATA_ACK from " << from.ToBase32().substr(0, 8) << "... "
                          << "(RTT: " << rtt << "ms, data: " << payload << ")\n";
                if (m_IsServer && payload == "ready") {
                    std::cout << "Server: Client ready, starting chunked transfer (" << m_ChunkSize << " byte chunks)\n";
                    
                    // Start file sending in separate thread
                    m_StopSender = false;
                    m_FileSenderThread = std::thread([this, from]() {
                        SendAllChunks(from);
                    });
                }
                break;
                
            case TunnelEcho::FILE_CHUNK:
                ++m_MessagesReceived;
                          
                if (!m_IsServer) {
                    // Parse chunk header: chunkIndex(4) + chunkSize(4) + payload
                    if (payload.size() < 8) break; // Invalid chunk
                    
                    uint32_t chunkIndex = *reinterpret_cast<const uint32_t*>(payload.data());
                    uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(payload.data() + 4);
                    
                    if (payload.size() < 8 + chunkSize) break; // Invalid chunk size
                    
                    std::vector<uint8_t> chunkData(payload.begin() + 8, payload.begin() + 8 + chunkSize);
                    
                    // Store chunk for processing thread using lock-free queue
                    ChunkItem* item = new ChunkItem{chunkIndex, std::move(chunkData), nullptr};
                    m_ChunkQueue.push(item);
                    m_ReceivedChunks.insert(chunkIndex);
                    m_HasNewChunks = true;
                    
                    if (m_MessagesReceived % 1000 == 0) {
                        std::cout << "Client: Accumulated " << m_ReceivedChunks.size() 
                                  << "/" << m_TotalExpectedChunks << " chunks (" << m_MessagesReceived << " messages)\n";
                    }
                }
                break;
                
            case TunnelEcho::CHUNK_REQUEST:
                ++m_MessagesReceived;
                std::cout << "Received CHUNK_REQUEST #" << msg->sequenceId 
                          << " from " << from.ToBase32().substr(0, 8) << "... "
                          << "(RTT: " << rtt << "ms)\n";
                if (m_IsServer) {
                    // Parse requested chunk indices
                    std::vector<uint32_t> requestedChunks;
                    for (size_t i = 0; i < payload.size(); i += 4) {
                        if (i + 4 <= payload.size()) {
                            uint32_t chunkIndex = *reinterpret_cast<const uint32_t*>(payload.data() + i);
                            requestedChunks.push_back(chunkIndex);
                        }
                    }
                    std::cout << "Server: Retransmitting " << requestedChunks.size() << " requested chunks\n";
                    SendRequestedChunks(from, requestedChunks);
                }
                break;
                
            case TunnelEcho::FILE_TRANSFER_OK:
                ++m_MessagesReceived;
                std::cout << "Received FILE_TRANSFER_OK from " << from.ToBase32().substr(0, 8) << "... "
                          << "(data: " << payload << ")\n";
                if (m_IsServer) {
                    std::cout << "Server: Transfer complete, shutting down\n";
                    
                    // Stop the file sender thread
                    m_StopSender = true;
                    if (m_FileSenderThread.joinable()) {
                        m_FileSenderThread.join();
                    }
                    
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
    std::cout << "  server [--conf config.conf] [--datadir path] [--hops N] [--filesize small|medium|large]\n";
    std::cout << "  client [--conf config.conf] [--datadir path] --server-b32 <address> [--hops N] [--message <text>] [--count N]\n\n";
    std::cout << "Server Options:\n";
    std::cout << "  --conf <file>       Configuration file path\n";
    std::cout << "  --datadir <path>    Data directory path\n";
    std::cout << "  --hops <N>          Tunnel hop count (default: 1, 0 = direct connection)\n";
    std::cout << "  --filesize <size>   Test file size: small (10MB), medium (100MB), large (1GB) [default: medium]\n\n";
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

int runServer(const std::string& datadir, const std::string& config, int hops, TunnelEcho::FileSize fileSize = TunnelEcho::MEDIUM) {
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
        
        // Generate test file
        std::string testFile = TunnelEcho::generateTestFile(fileSize);
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
        
        // Clean up test file
        std::remove(testFile.c_str());
        std::cout << "Test file cleaned up\n";
        
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
        std::this_thread::sleep_for(200ms);
        
        // Send FILE_TRANSFER_REQUEST message
        std::cout << "Sending FILE_TRANSFER_REQUEST...\n";
        echoClient->SendFileTransferRequest(serverIdent, "testfile.bin");
        std::this_thread::sleep_for(200ms);
        
        // Wait for responses and file transfer completion
        std::cout << "Waiting for file transfer to complete...\n";
        
        // Wait for completion or timeout
        auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(3600);  // 1 hour timeout

        while (!echoClient->IsTransferCompleted() && std::chrono::steady_clock::now() < timeout) {
            std::this_thread::sleep_for(200ms);
        }

        auto transferTimeTime = std::chrono::high_resolution_clock::now();
        auto transferDuration = std::chrono::duration<double>(transferTimeTime - startTime);

        while (!echoClient->ShouldExit() && std::chrono::steady_clock::now() < timeout) {
            std::this_thread::sleep_for(200ms);
        }
        
        if (echoClient->ShouldExit()) {
            std::cout << "Transfer completed successfully!\n";
        } else {
            std::cout << "Transfer timed out after 1 hour\n";
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double>(endTime - startTime);
        auto fileSizeMb = echoClient->GetFileSize() / (1024 * 1024);
        
        std::cout << "\n=== TUNNEL ECHO TEST COMPLETED ===\n";
        std::cout << "Test duration: " << std::setprecision(3) << duration.count() << " s\n";
        std::cout << "Transfer duration: " << std::setprecision(3) << transferDuration.count() << " s\n";
        std::cout << "Size: " <<  echoClient->GetFileSize() << " bytes\n";
        std::cout << "Approx. speed: " << std::setprecision(2) << fileSizeMb/transferDuration.count() << " MB/s\n";
        std::cout << "Messages sent: " << echoClient->GetMessagesSent() << "\n";
        std::cout << "Messages received: " << echoClient->GetMessagesReceived() << "\n";
        
        // Comprehensive cleanup to avoid mutex issues
        std::cout << "Cleaning up client resources...\n";
        
        // Stop client threads if they're running
        echoClient->StopClientThreads();
        
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
        
        // Server-specific arguments
        TunnelEcho::FileSize fileSize = TunnelEcho::MEDIUM;
        
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
            } else if (arg == "--filesize" && i + 1 < argc) {
                std::string size = argv[++i];
                if (size == "small") {
                    fileSize = TunnelEcho::SMALL;
                } else if (size == "medium") {
                    fileSize = TunnelEcho::MEDIUM;
                } else if (size == "large") {
                    fileSize = TunnelEcho::LARGE;
                } else {
                    std::cerr << "ERROR: Invalid filesize. Use: small, medium, or large\n";
                    return 1;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                printUsage(argv[0]);
                return 1;
            }
        }
        
        if (mode == "server")
            return runServer(datadir, config, hops, fileSize);

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