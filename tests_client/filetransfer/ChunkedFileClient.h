#pragma once

#include "IFileTransfer.h"
#include "FileTransferProtocol.h"
#include "Destination.h"
#include "Streaming.h"
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
    
public:
    explicit ChunkedFileClient(std::shared_ptr<client::ClientDestination> destination);
    ~ChunkedFileClient() = default;
    
    /**
     * @brief Request and download a file from server
     * @param serverB32 Server's base32 address
     * @param filename Name of file to download
     * @param timeout_ms Total timeout for entire transfer
     * @return Transfer statistics and downloaded data
     */
    struct TransferResult {
        TransferStats stats;
        std::vector<uint8_t> data;
        bool success;
        std::string error;
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
};

} // namespace i2p::filetransfer