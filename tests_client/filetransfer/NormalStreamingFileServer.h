#pragma once

#include "IFileTransfer.h"
#include "BinaryFileProtocol.h"
#include "../transport/NormalStreamingImpl.h"
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <mutex>

namespace i2p::filetransfer
{

/**
 * @brief High-performance file transfer server using Normal Streaming protocol
 */
class NormalStreamingFileServer : public IFileTransferServer
{
private:
    std::unique_ptr<embed::NormalStreamServer> m_streamServer;
    std::shared_ptr<client::ClientDestination> m_destination;
    mutable std::string m_lastStatus;
    
    // File storage with binary metadata
    std::map<std::string, std::vector<uint8_t>> m_files;
    std::map<std::string, BinaryFileMetadata> m_fileMetadata;
    mutable std::mutex m_filesMutex;
    
public:
    explicit NormalStreamingFileServer(std::shared_ptr<client::ClientDestination> destination);
    ~NormalStreamingFileServer() override = default;
    
    // IFileTransferServer interface
    void addMockFile(const std::string& filename, const std::vector<uint8_t>& data) override;
    void generateMockFile(const std::string& filename, size_t size, const std::string& seed = "") override;
    
    void start() override;
    void stop() override;
    
    std::string getB32Address() const override;
    bool isReady() const override;
    std::string getStatus() const override;
    std::vector<std::string> getFileList() const override;

private:
    /**
     * @brief Handle incoming file transfer requests
     */
    std::string handleFileTransferMessage(const std::string& message);
    
    /**
     * @brief Handle binary file request and return metadata
     */
    std::string handleBinaryFileRequest(const std::string& filename);
    
    /**
     * @brief Handle streaming chunk request  
     */
    std::string handleStreamFileRequest(const std::string& filename);
    
    /**
     * @brief Get file data safely
     */
    std::vector<uint8_t> getFileData(const std::string& filename) const;
    
    /**
     * @brief Get file metadata safely
     */
    BinaryFileMetadata getFileMetadata(const std::string& filename) const;
};

} // namespace i2p::filetransfer