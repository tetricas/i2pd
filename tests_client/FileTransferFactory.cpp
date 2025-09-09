#include "FileTransferFactory.h"
#include "SimpleMessagingFileClient.h"
#include "NormalStreamingFileClient.h"
#include "NormalStreamingFileServer.h"
#include "IFileTransfer.h"
#include "FileTransferLogging.h"

namespace i2p::filetransfer
{

std::unique_ptr<IFileTransferClient> FileTransferFactory::createClient(
    TransferProtocol protocol, 
    std::shared_ptr<client::ClientDestination> destination)
{
    if (!destination) {
        FT_LOG_ERROR("Factory", "Invalid destination provided");
        return nullptr;
    }
    
    switch (protocol) {
        case TransferProtocol::SIMPLE_MESSAGING:
            FT_LOG_DEBUG("Factory", "Creating Simple Messaging file client");
            return std::make_unique<SimpleMessagingFileClient>(destination);
            
        case TransferProtocol::NORMAL_STREAMING:
            FT_LOG_DEBUG("Factory", "Creating Normal Streaming file client");
            return std::make_unique<NormalStreamingFileClient>(destination);
            
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
    
    switch (protocol) {
        case TransferProtocol::SIMPLE_MESSAGING:
            FT_LOG_DEBUG("Factory", "Creating Simple Messaging file server");
            return std::make_unique<SimpleMessagingFileServer>(destination);
            
        case TransferProtocol::NORMAL_STREAMING:
            FT_LOG_DEBUG("Factory", "Creating Normal Streaming file server");
            return std::make_unique<NormalStreamingFileServer>(destination);
            
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