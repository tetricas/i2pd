#include "FileTransferFactory.h"
#include "ChunkedFileClient.h"
#include "ChunkedFileServer.h"
#include "IFileTransfer.h"
#include "../core/FileTransferLogging.h"

namespace i2p::filetransfer
{

// Adapter class to wrap ChunkedFileClient for IFileTransferClient compatibility
class ChunkedFileClientAdapter : public IFileTransferClient {
private:
    std::unique_ptr<ChunkedFileClient> m_impl;
    
public:
    explicit ChunkedFileClientAdapter(std::shared_ptr<client::ClientDestination> destination)
        : m_impl(std::make_unique<ChunkedFileClient>(destination)) {}
    
    TransferResult downloadFile(const std::string& serverB32, 
                               const std::string& filename,
                               int timeout_ms = 30000) override {
        auto result = m_impl->requestFile(serverB32, filename, timeout_ms);
        
        // Convert ChunkedFileClient::TransferResult to IFileTransferClient::TransferResult
        IFileTransferClient::TransferResult interfaceResult;
        interfaceResult.success = result.success;
        interfaceResult.error = result.error;
        interfaceResult.stats = result.stats;
        interfaceResult.data = result.getData(); // Use helper method for data access
        
        return interfaceResult;
    }
    
    bool isReady() const override {
        return m_impl->isReady();
    }
    
    std::string getStatus() const override {
        return m_impl->getStatus();
    }
};

// Adapter class to wrap ChunkedFileServer for IFileTransferServer compatibility  
class ChunkedFileServerAdapter : public IFileTransferServer {
private:
    std::unique_ptr<ChunkedFileServer> m_impl;
    
public:
    explicit ChunkedFileServerAdapter(std::shared_ptr<client::ClientDestination> destination)
        : m_impl(std::make_unique<ChunkedFileServer>(destination)) {}
    
    void addMockFile(const std::string& filename, const std::vector<uint8_t>& data) override {
        m_impl->addMockFile(filename, data);
    }
    
    void generateMockFile(const std::string& filename, size_t size, const std::string& seed = "") override {
        m_impl->generateMockFile(filename, size, seed);
    }
    
    void start() override {
        m_impl->start();
    }
    
    void stop() override {
        m_impl->stop();
    }
    
    std::string getB32Address() const override {
        return m_impl->getB32Address();
    }
    
    bool isReady() const override {
        return m_impl->isReady();
    }
    
    std::string getStatus() const override {
        return m_impl->getStatus();
    }
    
    std::vector<std::string> getFileList() const override {
        return m_impl->getFileList();
    }
};

std::unique_ptr<IFileTransferClient> FileTransferFactory::createClient(
    TransferProtocol protocol, 
    std::shared_ptr<client::ClientDestination> destination)
{
    if (!destination) {
        FT_LOG_ERROR("Factory", "Invalid destination provided");
        return nullptr;
    }
    
    // Use ChunkedFileClient for all protocols - it handles both Simple Messaging and Normal Streaming internally
    switch (protocol) {
        case TransferProtocol::SIMPLE_MESSAGING:
            FT_LOG_DEBUG("Factory", "Creating ChunkedFileClient (Simple Messaging protocol)");
            return std::make_unique<ChunkedFileClientAdapter>(destination);
            
        case TransferProtocol::NORMAL_STREAMING:
            FT_LOG_DEBUG("Factory", "Creating ChunkedFileClient (Normal Streaming protocol)");
            return std::make_unique<ChunkedFileClientAdapter>(destination);
            
        default:
            FT_LOG_ERROR("Factory", "Unknown protocol type: " << static_cast<int>(protocol));
            return nullptr;
    }
}

std::unique_ptr<IFileTransferServer> FileTransferFactory::createServer(
    TransferProtocol protocol, 
    std::shared_ptr<client::ClientDestination> destination)
{
    if (!destination) {
        FT_LOG_ERROR("Factory", "Invalid destination provided");
        return nullptr;
    }
    
    // Use ChunkedFileServer for all protocols - it handles both Simple Messaging and Normal Streaming internally
    switch (protocol) {
        case TransferProtocol::SIMPLE_MESSAGING:
            FT_LOG_DEBUG("Factory", "Creating ChunkedFileServer (Simple Messaging protocol)");
            return std::make_unique<ChunkedFileServerAdapter>(destination);
            
        case TransferProtocol::NORMAL_STREAMING:
            FT_LOG_DEBUG("Factory", "Creating ChunkedFileServer (Normal Streaming protocol)");
            return std::make_unique<ChunkedFileServerAdapter>(destination);
            
        default:
            FT_LOG_ERROR("Factory", "Unknown protocol type: " << static_cast<int>(protocol));
            return nullptr;
    }
}

std::string FileTransferFactory::getProtocolName(TransferProtocol protocol)
{
    switch (protocol) {
        case TransferProtocol::SIMPLE_MESSAGING:
            return "Simple Messaging";
        case TransferProtocol::NORMAL_STREAMING:
            return "Normal Streaming";
        default:
            return "Unknown";
    }
}

} // namespace i2p::filetransfer