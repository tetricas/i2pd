#include "ConnectionUtils.h"
#include "I2PdUtils.h"  
#include "FileTransferLogging.h"
#include <thread>
#include <chrono>

namespace i2p {
namespace filetransfer {

TransferResult<i2p::data::IdentHash> ConnectionUtils::parseServerAddress(const std::string& serverB32) {
    if (serverB32.empty()) {
        return TransferResult<i2p::data::IdentHash>::Error("Empty server address");
    }
    
    try {
        auto serverHash = embed::I2PdUtils::parseBase32(serverB32);
        FT_LOG_DEBUG("ConnectionUtils", "Parsed server address: " << serverB32);
        return TransferResult<i2p::data::IdentHash>::Success(std::move(serverHash));
    } catch (const std::exception& e) {
        std::string error = "Failed to parse server address '" + serverB32 + "': " + e.what();
        FT_LOG_ERROR("ConnectionUtils", error);
        return TransferResult<i2p::data::IdentHash>::Error(error);
    }
}

TransferResult<void> ConnectionUtils::waitForDestination(
    std::shared_ptr<client::ClientDestination> destination,
    const i2p::data::IdentHash& serverHash,
    int timeout_ms) {
    
    if (!destination) {
        return TransferResult<void>::Error("Invalid destination");
    }
    
    int timeout = getTimeoutOrDefault(timeout_ms);
    FT_LOG_DEBUG("ConnectionUtils", "Requesting server destination with timeout " << timeout << "ms");
    
    try {
        // Request destination and wait for server's LeaseSet to be available
        destination->RequestDestination(serverHash);
        
        FT_LOG_DEBUG("ConnectionUtils", "Waiting for server LeaseSet...");
        embed::I2PdUtils::waitForLeaseSet(serverHash, timeout);
        
        FT_LOG_DEBUG("ConnectionUtils", "Server LeaseSet obtained successfully");
        return TransferResult<void>::Success();
        
    } catch (const std::exception& e) {
        std::string error = "Exception while waiting for destination: " + std::string(e.what());
        FT_LOG_ERROR("ConnectionUtils", error);
        return TransferResult<void>::Error(error);
    }
}

TransferResult<std::shared_ptr<stream::Stream>> ConnectionUtils::createStream(
    std::shared_ptr<client::ClientDestination> destination,
    const i2p::data::IdentHash& serverHash,
    int timeout_ms) {
    
    if (!destination) {
        return TransferResult<std::shared_ptr<stream::Stream>>::Error("Invalid destination");
    }
    
    int maxRetries = TransferConfig::getMaxRetries();
    int retryDelay = TransferConfig::getRetryDelayMs();
    
    FT_LOG_DEBUG("ConnectionUtils", "Creating stream with " << maxRetries << " retries, " << retryDelay << "ms delay");
    
    std::shared_ptr<stream::Stream> stream;
    
    // Retry stream creation with configurable parameters instead of magic numbers
    for (int i = 0; i < maxRetries && !stream; ++i) {
        stream = destination->CreateStream(serverHash);
        if (!stream && i < maxRetries - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(retryDelay));
        }
    }
    
    if (!stream) {
        std::string error = "Failed to create stream after " + std::to_string(maxRetries) + " retries";
        FT_LOG_ERROR("ConnectionUtils", error);
        return TransferResult<std::shared_ptr<stream::Stream>>::Error(error);
    }
    
    FT_LOG_DEBUG("ConnectionUtils", "Stream created successfully, status: " << stream->GetStatus());
    
    // Log hop configuration for the destination that created this stream
    auto streamingDest = destination->GetStreamingDestination();
    if (streamingDest) {
        auto tunnelPool = destination->GetTunnelPool();
        if (tunnelPool) {
            // Note: Stream will use tunnels from this pool, hop count will be logged when actual tunnel is selected
            FT_LOG_DEBUG("ConnectionUtils", "Stream will use tunnels from pool associated with destination");
        }
    }
    
    return TransferResult<std::shared_ptr<stream::Stream>>::Success(std::move(stream));
}

TransferResult<std::shared_ptr<stream::Stream>> ConnectionUtils::establishConnection(
    std::shared_ptr<client::ClientDestination> destination,
    const std::string& serverB32,
    int timeout_ms) {
    
    FT_LOG_INFO("ConnectionUtils", "Establishing connection to: " << serverB32);
    
    // Step 1: Parse server address
    auto serverHashResult = parseServerAddress(serverB32);
    if (!serverHashResult) {
        return TransferResult<std::shared_ptr<stream::Stream>>::Error(serverHashResult.getError());
    }
    
    // Step 2: Wait for destination
    auto waitResult = waitForDestination(destination, serverHashResult.getValue(), timeout_ms);
    if (!waitResult) {
        return TransferResult<std::shared_ptr<stream::Stream>>::Error(waitResult.getError());
    }
    
    // Step 3: Create stream
    auto streamResult = createStream(destination, serverHashResult.getValue(), timeout_ms);
    if (!streamResult) {
        return TransferResult<std::shared_ptr<stream::Stream>>::Error(streamResult.getError());
    }
    
    FT_LOG_INFO("ConnectionUtils", "Connection established successfully");
    return streamResult;
}

TransferResult<void> ConnectionUtils::waitForStreamEstablishment(
    std::shared_ptr<stream::Stream> stream,
    int timeout_ms) {
    
    if (!stream) {
        return TransferResult<void>::Error("Invalid stream");
    }
    
    int timeout = getTimeoutOrDefault(timeout_ms);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    
    FT_LOG_DEBUG("ConnectionUtils", "Waiting for stream establishment, timeout: " << timeout << "ms");
    
    while (std::chrono::steady_clock::now() < deadline) {
        if (stream->IsEstablished()) {
            FT_LOG_DEBUG("ConnectionUtils", "Stream established successfully");
            return TransferResult<void>::Success();
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Small polling interval
    }
    
    std::string error = "Timeout waiting for stream establishment (" + std::to_string(timeout) + "ms)";
    FT_LOG_ERROR("ConnectionUtils", error);
    return TransferResult<void>::Error(error);
}

TransferResult<void> ConnectionUtils::sendHandshake(std::shared_ptr<stream::Stream> stream) {
    if (!stream) {
        return TransferResult<void>::Error("Invalid stream");
    }
    
    FT_LOG_DEBUG("ConnectionUtils", "Sending normal streaming handshake");
    
    try {
        // Send the required handshake for normal streaming
        auto sent = stream->Send(nullptr, 0);
        if (sent != 0) {
            std::string error = "Handshake send failed, expected 0 but got " + std::to_string(sent);
            FT_LOG_ERROR("ConnectionUtils", error);
            return TransferResult<void>::Error(error);
        }
        
        FT_LOG_DEBUG("ConnectionUtils", "Handshake sent successfully");
        return TransferResult<void>::Success();
        
    } catch (const std::exception& e) {
        std::string error = "Exception during handshake: " + std::string(e.what());
        FT_LOG_ERROR("ConnectionUtils", error);
        return TransferResult<void>::Error(error);
    }
}

} // namespace filetransfer
} // namespace i2p