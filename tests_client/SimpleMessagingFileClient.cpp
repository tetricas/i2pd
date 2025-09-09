#include "SimpleMessagingFileClient.h"
#include "ChunkedFileServer.h"
#include "Log.h"

namespace i2p::filetransfer
{

// Simple Messaging Client Implementation
SimpleMessagingFileClient::SimpleMessagingFileClient(std::shared_ptr<client::ClientDestination> destination)
    : m_impl(std::make_unique<ChunkedFileClient>(destination))
{
    LogPrint(eLogInfo, "SimpleMessagingFileClient: Created client wrapper");
}

IFileTransferClient::TransferResult SimpleMessagingFileClient::downloadFile(
    const std::string& serverB32, 
    const std::string& filename,
    int timeout_ms)
{
    LogPrint(eLogInfo, "SimpleMessagingFileClient: Starting file download: ", filename);
    
    auto result = m_impl->requestFile(serverB32, filename, timeout_ms);
    
    // Convert ChunkedFileClient::TransferResult to IFileTransferClient::TransferResult
    IFileTransferClient::TransferResult interfaceResult;
    interfaceResult.stats = result.stats;
    interfaceResult.data = std::move(result.data);
    interfaceResult.success = result.success;
    interfaceResult.error = result.error;
    
    return interfaceResult;
}

bool SimpleMessagingFileClient::isReady() const
{
    return m_impl->isReady();
}

std::string SimpleMessagingFileClient::getStatus() const
{
    return m_impl->getStatus();
}

// Simple Messaging Server Implementation
SimpleMessagingFileServer::SimpleMessagingFileServer(std::shared_ptr<client::ClientDestination> destination)
    : m_impl(std::make_unique<ChunkedFileServer>(destination))
{
    LogPrint(eLogInfo, "SimpleMessagingFileServer: Created server wrapper");
}

void SimpleMessagingFileServer::addMockFile(const std::string& filename, const std::vector<uint8_t>& data)
{
    m_impl->addMockFile(filename, data);
}

void SimpleMessagingFileServer::generateMockFile(const std::string& filename, size_t size, const std::string& seed)
{
    m_impl->generateMockFile(filename, size, seed);
}

void SimpleMessagingFileServer::start()
{
    m_impl->start();
}

void SimpleMessagingFileServer::stop()
{
    m_impl->stop();
}

std::string SimpleMessagingFileServer::getB32Address() const
{
    return m_impl->getB32Address();
}

bool SimpleMessagingFileServer::isReady() const
{
    return m_impl->isReady();
}

std::string SimpleMessagingFileServer::getStatus() const
{
    return m_impl->getStatus();
}

std::vector<std::string> SimpleMessagingFileServer::getFileList() const
{
    return m_impl->getFileList();
}

} // namespace i2p::filetransfer