#include "NormalStreamingFileClient.h"
#include "FileTransferProtocol.h"
#include "../core/StreamStabilityMonitor.h"
#include "../core/TransferRecovery.h"
#include "Log.h"
#include <algorithm>
#include <thread>
#include <chrono>
#include <iomanip>

namespace i2p::filetransfer
{

NormalStreamingFileClient::NormalStreamingFileClient(std::shared_ptr<client::ClientDestination> destination)
    : m_destination(std::move(destination))
{
    if (!m_destination) {
        m_lastStatus = "Invalid destination provided";
        LogPrint(eLogError, "NormalStreamingFileClient: Invalid destination");
        return;
    }
    
    // Create Normal Streaming client using proven Session 07 implementation
    m_streamClient = std::make_unique<embed::NormalStreamClient>(m_destination);
    
    m_lastStatus = "Normal streaming client created";
    LogPrint(eLogInfo, "NormalStreamingFileClient: Client created successfully");
}

IFileTransferClient::TransferResult NormalStreamingFileClient::downloadFile(
    const std::string& serverB32, 
    const std::string& filename,
    int timeout_ms)
{
    TransferResult result;
    result.success = false;
    result.stats.initTime = std::chrono::steady_clock::now();
    
    if (!isReady()) {
        result.error = "Client not ready: " + m_lastStatus;
        return result;
    }
    
    // Set up recovery system
    std::string transferId = serverB32 + ":" + filename;
    m_recoveryGuard = std::make_unique<i2p::core::TransferRecoveryGuard>(
        transferId,
        [this](const i2p::core::TransferCheckpoint& checkpoint, i2p::core::RecoveryStrategy strategy) {
            return handleRecovery(checkpoint, strategy);
        }
    );
    
    // Try to load existing checkpoint
    i2p::core::TransferCheckpoint checkpoint;
    if (m_recoveryGuard->loadCheckpoint(checkpoint)) {
        LogPrint(eLogInfo, "NormalStreamingFileClient: Resuming from checkpoint - ", 
                 std::fixed, std::setprecision(1), checkpoint.getProgress() * 100, "% complete");
        return resumeTransfer(checkpoint, timeout_ms);
    }
    
    LogPrint(eLogInfo, "NormalStreamingFileClient: Starting fresh download - ", filename);
    
    // Initialize checkpoint
    checkpoint.serverB32 = serverB32;
    checkpoint.filename = filename;
    checkpoint.lastUpdate = std::chrono::steady_clock::now();
    checkpoint.attemptCount = 0;
    
    try {
        // Step 1: Request file metadata using binary protocol
        result.stats.requestTime = std::chrono::steady_clock::now();
        auto metadata = requestFileMetadata(serverB32, filename, timeout_ms / 3);
        
        if (metadata.totalSize == 0) {
            result.error = "Invalid metadata received";
            return result;
        }
        
        LogPrint(eLogInfo, "NormalStreamingFileClient: Metadata received - size=", static_cast<uint64_t>(metadata.totalSize), 
                 " chunks=", static_cast<uint32_t>(metadata.chunkCount));
        
        // Update checkpoint with metadata
        checkpoint.totalSize = metadata.totalSize;
        checkpoint.checksum.assign(metadata.checksum, metadata.checksum + 32);
        m_recoveryGuard->saveCheckpoint(checkpoint);
        
        // Step 2: Download file using streaming chunks with monitoring
        result.stats.startTime = std::chrono::steady_clock::now();
        auto fileData = downloadFileStreamWithRecovery(serverB32, metadata, checkpoint, (timeout_ms * 2) / 3);
        result.stats.endTime = std::chrono::steady_clock::now();
        
        // Step 3: Verify data integrity with safe cleanup
        if (!fileData.empty() && validatePartialData(fileData, metadata.totalSize) && 
            verifyDownloadedData(fileData, metadata)) {
            result.data = std::move(fileData);
            result.success = true;
            result.stats.totalBytes = result.data.size();
            result.stats.chunksReceived = metadata.chunkCount;
            result.stats.chunksTotal = metadata.chunkCount;
            result.stats.verified = true;
            
            LogPrint(eLogInfo, "NormalStreamingFileClient: Download successful - ", result.data.size(), 
                     " bytes in ", result.stats.getTransferTime().count(), " ms");
            LogPrint(eLogInfo, "NormalStreamingFileClient: Throughput: ", 
                     std::fixed, std::setprecision(2), result.stats.getThroughputKBps(), " KB/s");
        } else {
            result.error = fileData.empty() ? "No data received" : "Data verification failed";
            LogPrint(eLogError, "NormalStreamingFileClient: ", result.error);
            
            // Update checkpoint with partial data for recovery
            checkpoint.bytesReceived = fileData.size();
            checkpoint.partialData = std::move(fileData);
            checkpoint.lastUpdate = std::chrono::steady_clock::now();
            if (m_recoveryGuard) {
                m_recoveryGuard->saveCheckpoint(checkpoint);
            }
        }
        
    } catch (const std::exception& e) {
        result.error = "Download exception: " + std::string(e.what());
        LogPrint(eLogError, "NormalStreamingFileClient: Exception: ", e.what());
    }
    
    // Always clear recovery guard to prevent dangling callback references
    if (m_recoveryGuard) {
        m_recoveryGuard.reset();
    }
    
    m_lastStatus = result.success ? "Download completed successfully" : result.error;
    return result;
}

bool NormalStreamingFileClient::isReady() const
{
    return m_streamClient && m_streamClient->isReady() && m_destination;
}

std::string NormalStreamingFileClient::getStatus() const
{
    if (!m_streamClient) return "Stream client not initialized";
    if (!m_destination) return "Destination not available";
    
    return "Ready: " + std::string(isReady() ? "yes" : "no") + ", " + m_lastStatus;
}

BinaryFileMetadata NormalStreamingFileClient::requestFileMetadata(
    const std::string& serverB32, 
    const std::string& filename, 
    int timeout_ms)
{
    LogPrint(eLogInfo, "NormalStreamingFileClient: Requesting metadata for: ", filename);
    
    // Create binary file request
    auto request = BinaryProtocolUtils::createFileRequest(filename);
    if (request.empty()) {
        throw std::runtime_error("Failed to create file request");
    }
    
    // Send request using normal streaming
    std::string requestStr(reinterpret_cast<const char*>(request.data()), request.size());
    auto response = m_streamClient->sendMessage(serverB32, requestStr, timeout_ms);
    
    if (response.empty()) {
        throw std::runtime_error("No metadata response received");
    }
    
    // Parse binary metadata response
    BinaryFileMetadata metadata{};
    if (!BinaryProtocolUtils::parseMetadata(
            reinterpret_cast<const uint8_t*>(response.data()), 
            response.size(), 
            metadata)) {
        throw std::runtime_error("Failed to parse metadata response");
    }
    
    return metadata;
}

std::vector<uint8_t> NormalStreamingFileClient::downloadFileStream(
    const std::string& serverB32,
    const BinaryFileMetadata& metadata,
    int timeout_ms)
{
    LogPrint(eLogInfo, "NormalStreamingFileClient: Starting continuous stream download - ", metadata.totalSize, " bytes");
    
    std::vector<uint8_t> fileData;
    fileData.reserve(metadata.totalSize);
    
    auto startTime = std::chrono::steady_clock::now();
    
    // TRUE STREAMING: Make ONE request and receive continuous stream
    auto streamRequest = std::string("STREAM_FILE:") + metadata.filename;
    LogPrint(eLogInfo, "NormalStreamingFileClient: Requesting continuous stream for ", metadata.filename);
    
    auto streamResponse = m_streamClient->sendMessage(serverB32, streamRequest, timeout_ms);
    
    if (streamResponse.empty()) {
        LogPrint(eLogError, "NormalStreamingFileClient: No stream response received");
        return {};
    }
    
    // Receive all file data in one continuous stream
    fileData.assign(streamResponse.begin(), streamResponse.end());
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);
    double throughputKBps = (fileData.size() / 1024.0) / (elapsed.count() / 1000.0);
    
    LogPrint(eLogInfo, "NormalStreamingFileClient: Stream download complete - ", fileData.size(), 
             " bytes in ", elapsed.count(), " ms (", 
             std::fixed, std::setprecision(2), throughputKBps, " KB/s)");
    
    return fileData;
}

bool NormalStreamingFileClient::verifyDownloadedData(
    const std::vector<uint8_t>& data, 
    const BinaryFileMetadata& metadata) const
{
    if (data.size() != metadata.totalSize) {
        LogPrint(eLogError, "NormalStreamingFileClient: Size mismatch - expected ", metadata.totalSize, 
                 " got ", data.size());
        return false;
    }
    
    // Verify checksum
    if (!BinaryProtocolUtils::verifyChecksum(data, metadata.checksum)) {
        LogPrint(eLogError, "NormalStreamingFileClient: Checksum verification failed");
        return false;
    }
    
    LogPrint(eLogInfo, "NormalStreamingFileClient: Data verification successful");
    return true;
}

bool NormalStreamingFileClient::handleRecovery(const i2p::core::TransferCheckpoint& checkpoint, 
                                              i2p::core::RecoveryStrategy strategy) {
    if (m_recoveryInProgress.exchange(true)) {
        LogPrint(eLogWarning, "NormalStreamingFileClient: Recovery already in progress");
        return false;
    }
    
    LogPrint(eLogInfo, "NormalStreamingFileClient: Handling recovery for ", checkpoint.filename, 
             " using strategy ", (int)strategy);
    
    bool success = false;
    
    try {
        switch (strategy) {
            case i2p::core::RecoveryStrategy::IMMEDIATE_RETRY:
                success = downloadFile(checkpoint.serverB32, checkpoint.filename, 30000).success;
                break;
                
            case i2p::core::RecoveryStrategy::DELAYED_RETRY:
                std::this_thread::sleep_for(std::chrono::seconds(5));
                success = downloadFile(checkpoint.serverB32, checkpoint.filename, 30000).success;
                break;
                
            case i2p::core::RecoveryStrategy::CHECKPOINT_RESUME:
                success = resumeTransfer(checkpoint, 30000).success;
                break;
                
            default:
                LogPrint(eLogWarning, "NormalStreamingFileClient: Unsupported recovery strategy");
                break;
        }
    } catch (const std::exception& e) {
        LogPrint(eLogError, "NormalStreamingFileClient: Recovery exception: ", e.what());
    }
    
    m_recoveryInProgress = false;
    return success;
}

IFileTransferClient::TransferResult NormalStreamingFileClient::resumeTransfer(
    const i2p::core::TransferCheckpoint& checkpoint, int timeout_ms) {
    
    TransferResult result;
    result.success = false;
    
    if (!checkpoint.isValid()) {
        result.error = "Invalid checkpoint";
        return result;
    }
    
    LogPrint(eLogInfo, "NormalStreamingFileClient: Resuming transfer from ", 
             checkpoint.bytesReceived, "/", checkpoint.totalSize, " bytes");
    
    // For streaming protocol, we need to restart from beginning
    // TODO: Implement actual resume capability when protocol supports it
    LogPrint(eLogInfo, "NormalStreamingFileClient: Streaming protocol requires full restart");
    
    return downloadFile(checkpoint.serverB32, checkpoint.filename, timeout_ms);
}

bool NormalStreamingFileClient::validatePartialData(const std::vector<uint8_t>& data, 
                                                   size_t expectedSize) const {
    if (data.empty()) {
        LogPrint(eLogWarning, "NormalStreamingFileClient: No data to validate");
        return false;
    }
    
    if (data.size() > expectedSize) {
        LogPrint(eLogError, "NormalStreamingFileClient: Data size exceeds expected - got ", 
                 data.size(), " expected ", expectedSize);
        return false;
    }
    
    // Check for obvious corruption patterns
    bool hasNonZero = false;
    for (size_t i = 0; i < std::min(data.size(), size_t(1024)); ++i) {
        if (data[i] != 0) {
            hasNonZero = true;
            break;
        }
    }
    
    if (!hasNonZero) {
        LogPrint(eLogWarning, "NormalStreamingFileClient: Data appears to be all zeros");
        return false;
    }
    
    LogPrint(eLogInfo, "NormalStreamingFileClient: Partial data validation passed - ", 
             data.size(), " bytes");
    return true;
}

std::vector<uint8_t> NormalStreamingFileClient::downloadFileStreamWithRecovery(
    const std::string& serverB32,
    const BinaryFileMetadata& metadata,
    i2p::core::TransferCheckpoint& checkpoint,
    int timeout_ms) {
    
    LogPrint(eLogInfo, "NormalStreamingFileClient: Starting monitored stream download");
    
    // Create stream monitor for this transfer
    auto streamId = std::make_pair(rand(), rand()); // Generate stream ID
    auto streamGuard = std::make_unique<i2p::core::StreamMonitorGuard>(streamId);
    
    // Associate stream with transfer for recovery
    if (m_recoveryGuard) {
        m_recoveryGuard->associateStream(streamId);
    }
    
    // Set up failure callback for immediate recovery
    auto& monitor = i2p::core::StreamStabilityMonitor::getInstance();
    monitor.setFailureCallback([this, &checkpoint, serverB32, &metadata, timeout_ms](
        i2p::core::StreamStabilityMonitor::StreamID sid, const i2p::core::StreamHealth& health) {
        
        LogPrint(eLogWarning, "NormalStreamingFileClient: Stream failure detected - resends:", 
                 health.resendCount, " tunnelSwitches:", health.tunnelSwitches);
        
        if (health.requiresRecovery()) {
            LogPrint(eLogInfo, "NormalStreamingFileClient: Triggering recovery due to stream instability");
            auto& recovery = i2p::core::TransferRecovery::getInstance();
            recovery.handleStreamFailure(sid, health);
        }
    });
    
    try {
        auto result = downloadFileStream(serverB32, metadata, timeout_ms);
        
        // Update checkpoint during download
        if (!result.empty()) {
            checkpoint.bytesReceived = result.size();
            checkpoint.lastUpdate = std::chrono::steady_clock::now();
            if (m_recoveryGuard) {
                m_recoveryGuard->saveCheckpoint(checkpoint);
            }
            streamGuard->recordThroughput((result.size() / 1024.0) / (timeout_ms / 1000.0));
        }
        
        return result;
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "NormalStreamingFileClient: Stream download failed: ", e.what());
        streamGuard->markUnstable();
        throw;
    }
}

} // namespace i2p::filetransfer