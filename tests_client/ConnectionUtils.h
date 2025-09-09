#pragma once

#include "TransferResult.h"
#include "TransferConfig.h"
#include "Destination.h"
#include "Streaming.h"
#include "Identity.h"
#include <memory>
#include <string>

namespace i2p {
namespace filetransfer {

/**
 * @brief Utility functions for common connection establishment patterns
 */
class ConnectionUtils {
public:
    /**
     * @brief Parse a base32 server address into IdentHash
     * @param serverB32 Server's base32 address  
     * @return TransferResult containing IdentHash on success, error message on failure
     */
    static TransferResult<i2p::data::IdentHash> parseServerAddress(const std::string& serverB32);
    
    /**
     * @brief Wait for destination to be ready and request server destination
     * @param destination Local client destination
     * @param serverHash Server's identity hash
     * @param timeout_ms Timeout in milliseconds
     * @return TransferResult indicating success or failure
     */
    static TransferResult<void> waitForDestination(
        std::shared_ptr<client::ClientDestination> destination,
        const i2p::data::IdentHash& serverHash,
        int timeout_ms = -1);
        
    /**
     * @brief Create a stream to the specified server with retries
     * @param destination Local client destination
     * @param serverHash Server's identity hash
     * @param timeout_ms Total timeout in milliseconds
     * @return TransferResult containing stream on success, error message on failure
     */
    static TransferResult<std::shared_ptr<stream::Stream>> createStream(
        std::shared_ptr<client::ClientDestination> destination,
        const i2p::data::IdentHash& serverHash,
        int timeout_ms = -1);
        
    /**
     * @brief Complete connection establishment pattern used across multiple classes
     * This combines parseServerAddress + waitForDestination + createStream
     * @param destination Local client destination  
     * @param serverB32 Server's base32 address
     * @param timeout_ms Total timeout in milliseconds
     * @return TransferResult containing established stream on success
     */
    static TransferResult<std::shared_ptr<stream::Stream>> establishConnection(
        std::shared_ptr<client::ClientDestination> destination,
        const std::string& serverB32,
        int timeout_ms = -1);
        
    /**
     * @brief Wait for stream to be established with proper handshake
     * @param stream The stream to wait for establishment
     * @param timeout_ms Timeout in milliseconds
     * @return TransferResult indicating success or failure
     */
    static TransferResult<void> waitForStreamEstablishment(
        std::shared_ptr<stream::Stream> stream,
        int timeout_ms = -1);
        
    /**
     * @brief Send normal streaming handshake (Send(nullptr, 0))
     * @param stream The stream to send handshake on
     * @return TransferResult indicating success or failure
     */
    static TransferResult<void> sendHandshake(std::shared_ptr<stream::Stream> stream);

private:
    /**
     * @brief Get default timeout if not specified
     * @param specified_timeout User-specified timeout (-1 for default)
     * @return Actual timeout to use
     */
    static int getTimeoutOrDefault(int specified_timeout) {
        return specified_timeout > 0 ? specified_timeout : TransferConfig::getConnectionTimeout();
    }
};

} // namespace filetransfer
} // namespace i2p