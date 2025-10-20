#pragma once

#include "IFileTransfer.h"
#include "FileTransferProtocol.h"
#include "Destination.h"
#include "Streaming.h"
#include "../core/TransferRecovery.h"
#include "../core/StreamStabilityMonitor.h"
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>

// Forward declarations
namespace i2p::embed {
    class SimpleStreamClient;
}

namespace i2p::filetransfer
{

/**
 * @brief Client for chunked file transfers using SimpleSend/SimpleReceive protocol
 */
class ChunkedFileClient
{
private:
    std::shared_ptr<client::ClientDestination> m_destination;
    mutable std::string m_lastStatus;
    std::atomic<bool> m_transferActive{false};
    
    // New stream-based architecture
    std::mutex m_incomingStreamsMutex;
    std::condition_variable m_streamCondition;
    std::shared_ptr<stream::Stream> m_currentResponseStream;
    bool m_expectingResponse = false;
    
    // Recovery system
    std::unique_ptr<i2p::core::TransferRecoveryGuard> m_recoveryGuard;
    std::atomic<bool> m_recoveryInProgress{false};
    
public:
    explicit ChunkedFileClient(std::shared_ptr<client::ClientDestination> destination);
    ~ChunkedFileClient() {
        // Ensure recovery guard is properly cleaned up
        if (m_recoveryGuard) {
            m_recoveryGuard.reset();
        }
    }
    
    /**
     * @brief Request and download a file from server
     * @param serverB32 Server's base32 address
     * @param filename Name of file to download
     * @param timeout_ms Total timeout for entire transfer
     * @return Transfer statistics and downloaded data
     */
    struct TransferResult {
        TransferStats stats;
        std::vector<uint8_t> data;  // For backward compatibility - small files only
        bool success;
        std::string error;
        std::unique_ptr<TransferStorage> storage;  // Unified storage interface
        
        // Helper methods
        size_t getDataSize() const {
            return storage ? storage->size() : data.size();
        }
        
        std::vector<uint8_t> getData() const {
            if (storage) {
                auto storageData = storage->getData();
                return storageData.empty() ? data : storageData;
            }
            return data;
        }
        
        std::string getStoragePath() const {
            return storage ? storage->getStoragePath() : "legacy";
        }
    };
    
    TransferResult requestFile(const std::string& serverB32, 
                              const std::string& filename,
                              int timeout_ms = 30000);
    
    /**
     * @brief Get client status
     */
    bool isReady() const;
    std::string getStatus() const;
    
private:
    /**
     * @brief Handle incoming stream from server (new architecture)
     */
    void handleIncomingStream(std::shared_ptr<stream::Stream> stream);
    
    /**
     * @brief Receive complete file on server's dedicated stream
     */
    void receiveFileOnStream(std::shared_ptr<stream::Stream> stream,
                             const std::string& filename,
                             const std::string& serverB32,
                             int timeout_ms,
                             TransferResult& result);
    
    /**
     * @brief Read a message from stream
     */
    std::string readMessageFromStream(std::shared_ptr<stream::Stream> stream, int timeout_ms);
    
    /**
     * @brief Read chunk data from stream
     * @param stream Stream to read from
     * @param expectedSize Expected number of bytes to read
     * @param timeout_ms Timeout in milliseconds
     */  
    std::vector<uint8_t> readChunkFromStream(std::shared_ptr<stream::Stream> stream, size_t expectedSize, int timeout_ms);
    
    /**
     * @brief Send a protocol message and wait for response (legacy method)
     */
    std::string sendMessage(embed::SimpleStreamClient* client,
                           const std::string& serverB32,
                           MessageType type, 
                           const std::string& payload,
                           int timeout_ms);
    
    /**
     * @brief Parse metadata from server response
     */
    FileMetadata parseMetadata(const std::string& payload);
    
    /**
     * @brief Verify downloaded data against metadata
     */
    bool verifyData(const std::vector<uint8_t>& data, 
                   const FileMetadata& metadata) const;
    
    /**
     * @brief Enforce universal flow control (prevents overwhelming sender)
     */
    void enforceUniversalFlowControl();
    
    /**
     * @brief Handle transfer recovery
     */
    bool handleRecovery(const i2p::core::TransferCheckpoint& checkpoint, 
                       i2p::core::RecoveryStrategy strategy);
    
    /**
     * @brief Resume transfer from checkpoint  
     */
    TransferResult resumeTransfer(const i2p::core::TransferCheckpoint& checkpoint, 
                                 int timeout_ms);
    
    /**
     * @brief Validate partial data for integrity
     */
    bool validatePartialData(const std::vector<uint8_t>& data, 
                           size_t expectedSize) const;
};

} // namespace i2p::filetransfer