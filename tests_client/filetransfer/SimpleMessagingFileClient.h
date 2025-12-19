#pragma once

#include "IFileTransfer.h"
#include "ChunkedFileClient.h"
#include "ChunkedFileServer.h"
#include <memory>

namespace i2p::filetransfer
{

/**
 * @brief Simple messaging file transfer client (wrapper around ChunkedFileClient)
 */
class SimpleMessagingFileClient : public IFileTransferClient
{
private:
    std::unique_ptr<ChunkedFileClient> m_impl;
    
public:
    explicit SimpleMessagingFileClient(std::shared_ptr<client::ClientDestination> destination);
    ~SimpleMessagingFileClient() override = default;
    
    // IFileTransferClient interface
    TransferResult downloadFile(const std::string& serverB32, 
                               const std::string& filename,
                               int timeout_ms = 30000) override;
    
    bool isReady() const override;
    std::string getStatus() const override;
};

/**
 * @brief Simple messaging file transfer server (wrapper around ChunkedFileServer)
 */
class SimpleMessagingFileServer : public IFileTransferServer
{
private:
    std::unique_ptr<ChunkedFileServer> m_impl;
    
public:
    explicit SimpleMessagingFileServer(std::shared_ptr<client::ClientDestination> destination);
    ~SimpleMessagingFileServer() override = default;
    
    // IFileTransferServer interface
    void addMockFile(const std::string& filename, const std::vector<uint8_t>& data) override;
    void generateMockFile(const std::string& filename, size_t size, const std::string& seed = "") override;
    
    void start() override;
    void stop() override;
    
    std::string getB32Address() const override;
    bool isReady() const override;
    std::string getStatus() const override;
    std::vector<std::string> getFileList() const override;
};

} // namespace i2p::filetransfer