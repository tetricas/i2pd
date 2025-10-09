#pragma once

#include "FileTransferProtocol.h"
#include "Destination.h"
#include "Streaming.h"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <mutex>
#include <thread>

// Forward declarations
namespace i2p::embed {
    class SimpleStreamServer;
}

namespace i2p::filetransfer
{

/**
 * @brief Server for chunked file transfers using SimpleSend/SimpleReceive protocol
 */
class ChunkedFileServer
{
private:
    std::shared_ptr<client::ClientDestination> m_destination;
    std::unique_ptr<embed::SimpleStreamServer> m_server;
    std::atomic<bool> m_running{false};
    mutable std::string m_lastStatus;
    
    // File storage
    std::map<std::string, std::vector<uint8_t>> m_files;
    std::map<std::string, FileMetadata> m_fileMetadata;
    mutable std::mutex m_filesMutex;
    
public:
    explicit ChunkedFileServer(std::shared_ptr<client::ClientDestination> destination);
    ~ChunkedFileServer();
    
    /**
     * @brief Add a mock file to serve
     * @param filename Name of the file
     * @param data File content
     */
    void addMockFile(const std::string& filename, const std::vector<uint8_t>& data);
    
    /**
     * @brief Generate and add a mock file with specified size
     * @param filename Name of the file
     * @param size Size in bytes
     * @param seed Random seed for content generation
     */
    void generateMockFile(const std::string& filename, size_t size, const std::string& seed = "");
    
    /**
     * @brief Start server
     */
    void start();
    
    /**
     * @brief Stop server
     */
    void stop();
    
    /**
     * @brief Get server's base32 address
     */
    std::string getB32Address() const;
    
    /**
     * @brief Check if server is ready
     */
    bool isReady() const;
    
    /**
     * @brief Get server status
     */
    std::string getStatus() const;
    
    /**
     * @brief Get list of available files
     */
    std::vector<std::string> getFileList() const;
    
private:
    /**
     * @brief Handle incoming client messages (simple message handler)
     * @param clientMessage Raw message from client
     * @return Response message or empty for async processing
     */
    std::string handleClientMessage(const std::string& clientMessage, const i2p::data::IdentHash& clientHash);
    
    /**
     * @brief Handle file transfer on dedicated stream (new architecture)
     * @param filename File to transfer
     * @param clientHash Client's identity hash
     */
    void handleFileTransferOnStream(const std::string& filename, const i2p::data::IdentHash& clientHash);
    
    /**
     * @brief Send complete file on stream
     * @param stream Outbound stream to client
     * @param filename File to send
     * @return Success status
     */
    bool sendFileOnStream(std::shared_ptr<stream::Stream> stream, const std::string& filename);
    
    /**
     * @brief Send message on stream
     * @param stream Stream to write to
     * @param message Message to send
     * @return Bytes sent
     */
    size_t sendMessageOnStream(std::shared_ptr<stream::Stream> stream, const std::string& message);
    
    /**
     * @brief Try to load file from disk if not in registry
     * @param filename File to load
     * @return True if successfully loaded
     */
    bool tryLoadFileFromDisk(const std::string& filename);
    
    /**
     * @brief Send chunk data on stream
     * @param stream Stream to write to
     * @param chunkData Raw chunk bytes
     * @return Bytes sent
     */
    size_t sendChunkOnStream(std::shared_ptr<stream::Stream> stream, const std::vector<uint8_t>& chunkData);
    
    /**
     * @brief Parse filename from payload
     * @param payload Key=value formatted payload
     * @return Filename or empty string
     */
    std::string parseFilenameFromPayload(const std::string& payload);
    
    /**
     * @brief Enforce universal flow control (prevents queue overflow on all networks)
     */
    void enforceUniversalFlowControl();
    
    // Legacy methods (for compatibility)
    std::string handleFileRequest(const std::string& payload);
    std::string handleChunkRequest(const std::string& payload);  
    std::string handleTransferComplete(const std::string& payload);
    std::string createErrorResponse(ErrorCode errorCode, const std::string& message);
};

} // namespace i2p::filetransfer