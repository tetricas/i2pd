#pragma once

#include "IFileTransfer.h"
#include "Destination.h"
#include <memory>

namespace i2p::filetransfer
{

/**
 * @brief File transfer protocol types
 */
enum class TransferProtocol
{
    SIMPLE_MESSAGING,    // SimpleSend/SimpleReceive (Session 05)
    NORMAL_STREAMING     // Normal streaming (Session 08)
};

/**
 * @brief Factory for creating file transfer clients and servers
 */
class FileTransferFactory
{
public:
    /**
     * @brief Create a file transfer client
     * @param protocol Transport protocol to use
     * @param destination i2pd client destination
     * @return Unique pointer to client implementation
     */
    static std::unique_ptr<IFileTransferClient> createClient(
        TransferProtocol protocol, 
        std::shared_ptr<client::ClientDestination> destination);
    
    /**
     * @brief Create a file transfer server
     * @param protocol Transport protocol to use
     * @param destination i2pd client destination
     * @return Unique pointer to server implementation
     */
    static std::unique_ptr<IFileTransferServer> createServer(
        TransferProtocol protocol, 
        std::shared_ptr<client::ClientDestination> destination);
    
    /**
     * @brief Get protocol name for display
     * @param protocol Protocol enum value
     * @return Human-readable protocol name
     */
    static std::string getProtocolName(TransferProtocol protocol);
};

} // namespace i2p::filetransfer