#include "ChunkedFileClient.h"
#include "../core/I2PdUtils.h"
#include "../transport/SimpleStreamingImpl.h"
#include "../core/FileTransferLogging.h"
#include "../core/TransferConfig.h"
#include "../core/ConnectionUtils.h"
#include "../core/UniversalFlowControl.h"
#include "../core/StreamStabilityMonitor.h"
#include "../core/TransferRecovery.h"
#include "Log.h"
#include <sstream>
#include <iomanip>
using namespace std::chrono_literals;

namespace i2p::filetransfer
{

ChunkedFileClient::ChunkedFileClient(std::shared_ptr<client::ClientDestination> destination)
    : m_destination(std::move(destination))
{
    // Set up stream acceptor for new architecture
    if (m_destination) {
        // Get streaming destination to accept incoming streams from servers
        auto streamingDest = m_destination->GetStreamingDestination();
        if (streamingDest) {
            streamingDest->SetAcceptor([this](std::shared_ptr<stream::Stream> stream) {
                if (stream) {
                    FT_LOG_INFO("ChunkedFileClient", "Accepting incoming stream from server");
                    handleIncomingStream(stream);
                }
            });
            FT_LOG_INFO("ChunkedFileClient", "Stream acceptor configured");
        } else {
            FT_LOG_WARNING("ChunkedFileClient", "Could not get streaming destination");
        }
    }
}

ChunkedFileClient::TransferResult ChunkedFileClient::requestFile(
    const std::string& serverB32, 
    const std::string& filename,
    int timeout_ms)
{
    TransferResult result;
    result.success = false;
    
    if (!isReady()) {
        result.error = "Client destination not ready";
        m_lastStatus = result.error;
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
        LogPrint(eLogInfo, "ChunkedFileClient: Resuming from checkpoint - ", 
                 std::fixed, std::setprecision(1), checkpoint.getProgress() * 100, "% complete");
        return resumeTransfer(checkpoint, timeout_ms);
    }
    
    m_transferActive.store(true);
    // Note: startTime will be set when actual data reception begins
    
    // Initialize checkpoint
    checkpoint.serverB32 = serverB32;
    checkpoint.filename = filename;
    checkpoint.lastUpdate = std::chrono::steady_clock::now();
    checkpoint.attemptCount = 0;
    
    try {
        FT_LOG_INFO("ChunkedFileClient", "Starting fresh file transfer for: " << filename << " using new stream architecture");
        result.stats.initTime = std::chrono::steady_clock::now();

        // Step 1: Send file request via SimpleSend (lightweight)
        auto streamingDest = m_destination->GetStreamingDestination();
        if (!streamingDest) {
            result.error = "Could not get streaming destination";
            m_lastStatus = result.error;
            return result;
        }
        
        // Use ConnectionUtils for consistent connection establishment
        auto connectionResult = i2p::filetransfer::ConnectionUtils::establishConnection(
            m_destination, serverB32, timeout_ms / 2);
        if (!connectionResult) {
            result.error = connectionResult.getError();
            m_lastStatus = result.error;
            return result;
        }
        auto requestStream = connectionResult.getValue();
        
        
        // Send request via SimpleSend
        result.stats.requestTime = std::chrono::steady_clock::now();
        std::string request = "1:filename=" + filename;
        FT_LOG_INFO("ChunkedFileClient", "Sending request: " << request);
        requestStream->SimpleSend(request, true); // Expect response to trigger custom handler
        
        // Step 2: Wait for server to establish response stream
        {
            std::unique_lock<std::mutex> lock(m_incomingStreamsMutex);
            m_expectingResponse = true;
            m_currentResponseStream = nullptr;
            
            FT_LOG_INFO("ChunkedFileClient", "Waiting for server response stream...");
            
            if (!m_streamCondition.wait_for(lock, std::chrono::milliseconds(timeout_ms / 2), 
                [this] { return m_currentResponseStream != nullptr; })) {
                result.error = "Timeout waiting for server response stream";
                m_lastStatus = result.error;
                return result;
            }
            
            FT_LOG_INFO("ChunkedFileClient", "Server response stream established");
        }
        
        // Step 3: Receive complete file on server's dedicated stream
        receiveFileOnStream(m_currentResponseStream, filename, serverB32, timeout_ms / 2, result);
        
        if (result.success) {
            result.stats.endTime = std::chrono::steady_clock::now();
            m_lastStatus = "Transfer completed successfully";
            
            // Clear checkpoint on success
            m_recoveryGuard.reset();
            
            FT_LOG_INFO("ChunkedFileClient", "Transfer complete - " << result.getDataSize() << " bytes in " << result.stats.getTransferTime().count() << "ms, " << result.stats.getThroughputKBps() << " KB/s at " << result.getStoragePath());
        } else {
            // Save checkpoint for recovery using unified storage
            if (result.storage && result.storage->size() > 0) {
                checkpoint.bytesReceived = result.storage->size();
                checkpoint.storagePath = result.storage->getStoragePath();
                
                // For small files, also save data for compatibility
                auto data = result.getData();
                if (!data.empty() && validatePartialData(data, checkpoint.totalSize)) {
                    checkpoint.partialData = std::move(data);
                }
                
                checkpoint.lastUpdate = std::chrono::steady_clock::now();
                m_recoveryGuard->saveCheckpoint(checkpoint);
                LogPrint(eLogInfo, "ChunkedFileClient: Checkpoint saved for recovery - ", 
                         checkpoint.bytesReceived, " bytes at ", checkpoint.storagePath);
            }
        }
        
    } catch (const std::exception& e) {
        result.error = "Exception: " + std::string(e.what());
        m_lastStatus = result.error;
        FT_LOG_ERROR("ChunkedFileClient", "Exception: " << e.what());
    }
    
    // Always clear recovery guard to prevent dangling callback references
    if (m_recoveryGuard) {
        m_recoveryGuard.reset();
    }
    
    // Cleanup
    {
        std::lock_guard<std::mutex> lock(m_incomingStreamsMutex);
        m_expectingResponse = false;
        m_currentResponseStream = nullptr;
    }
    
    m_transferActive.store(false);
    return result;
}

bool ChunkedFileClient::isReady() const
{
    return m_destination && m_destination->IsReady();
}

std::string ChunkedFileClient::getStatus() const
{
    if (!m_destination)
        return "No destination";
        
    std::ostringstream oss;
    oss << "Destination ready: " << (m_destination->IsReady() ? "yes" : "no");
    oss << ", Transfer active: " << (m_transferActive.load() ? "yes" : "no");
    if (!m_lastStatus.empty())
        oss << ", Status: " << m_lastStatus;
        
    return oss.str();
}

void ChunkedFileClient::handleIncomingStream(std::shared_ptr<stream::Stream> stream)
{
    std::lock_guard<std::mutex> lock(m_incomingStreamsMutex);
    
    FT_LOG_INFO("ChunkedFileClient", "Incoming stream received from server");
    
    if (m_expectingResponse && !m_currentResponseStream) {
        m_currentResponseStream = stream;
        FT_LOG_INFO("ChunkedFileClient", "Stream assigned for file transfer");
        m_streamCondition.notify_one();
    } else {
        LogPrint(eLogWarning, "ChunkedFileClient: Unexpected incoming stream - expecting=", m_expectingResponse, ", current=", (m_currentResponseStream ? "set" : "null"));
    }
}

void ChunkedFileClient::receiveFileOnStream(
    std::shared_ptr<stream::Stream> stream,
    const std::string& filename,
    const std::string& serverB32,
    int timeout_ms,
    TransferResult& result)
{
    if (!stream) {
        result.error = "No stream provided";
        return;
    }
    
    try {
        result.stats.startTime = std::chrono::steady_clock::now();
        FT_LOG_INFO("ChunkedFileClient", "Starting file reception on stream");
        
        // Step 1: Read metadata message
        std::string metadataMsg = readMessageFromStream(stream, timeout_ms / 4);
        if (metadataMsg.empty()) {
            result.error = "Failed to receive metadata from stream";
            return;
        }
        
        // Parse metadata (should be "2:filename=...&size=...&checksum=..." - no chunk_count)
        size_t colonPos = metadataMsg.find(':');
        if (colonPos == std::string::npos || metadataMsg.substr(0, colonPos) != "2") {
            result.error = "Invalid metadata format: " + metadataMsg;
            return;
        }
        
        FileMetadata metadata = parseMetadata(metadataMsg.substr(colonPos + 1));
        if (metadata.filename.empty() || filename != metadata.filename) {
            result.error = "Wrong filename: " + metadata.filename;
            return;
        }
        
        FT_LOG_INFO("ChunkedFileClient", "Metadata received - Size: " << metadata.totalSize << ", Checksum: " << metadata.sha256Checksum.substr(0, 8) << "... (chunks will be discovered)");
        
        // Create appropriate storage based on file size
        const size_t LARGE_FILE_THRESHOLD = 100 * 1024 * 1024; // 100MB
        
        if (metadata.totalSize > LARGE_FILE_THRESHOLD) {
            // Use file-based storage for large files
            std::string tempFilePath = "/tmp/ft_" + filename + "_" + std::to_string(rand()) + ".tmp";
            auto fileStorage = std::make_unique<FileStorage>(tempFilePath);
            
            // Check if file was created successfully  
            try {
                fileStorage->write({});  // Test write
            } catch (const std::exception& e) {
                result.error = "Failed to create temporary file: " + tempFilePath + " - " + e.what();
                return;
            }
            
            FT_LOG_INFO("ChunkedFileClient", "Using file-based storage: " << tempFilePath);
            result.storage = std::move(fileStorage);
        } else {
            // Use memory storage for small files
            FT_LOG_INFO("ChunkedFileClient", "Using memory-based storage");
            result.storage = std::make_unique<MemoryStorage>();
            
            // Also keep legacy support
            result.data.reserve(metadata.totalSize);
        }
        
        // Step 2: Read chunks until we have all the data
        size_t chunkIndex = 0;
        int lastLoggedPercent = -1;
        
        while (result.storage->size() < metadata.totalSize) {
            if (!m_transferActive.load()) {
                result.error = "Transfer cancelled";
                return;
            }
            
            auto chunkStart = std::chrono::steady_clock::now();

            int currentPercent = static_cast<int>((100.0 * result.storage->size()) / metadata.totalSize);
            if (currentPercent / 10 > lastLoggedPercent / 10) {
                lastLoggedPercent = currentPercent;
                FT_LOG_INFO("ChunkedFileClient", "Reading chunk header for chunk " << chunkIndex << " (received " << result.storage->size() << "/" << metadata.totalSize << " bytes so far)");
            }
            // Read chunk header: [4 bytes chunk_index][4 bytes chunk_size]
            std::vector<uint8_t> header = readChunkFromStream(stream, 8, TransferConfig::getChunkTimeout());
            if (header.size() != 8) {
                // Detect potential stream failure (Session 14 recovery system)
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - chunkStart);
                
                if (elapsed.count() > 10 && m_recoveryGuard) {
                    // Likely stream failure - trigger recovery system
                    FT_LOG_WARNING("ChunkedFileClient", "Chunk timeout after " << elapsed.count() << "s - triggering recovery");
                    
                    // Save checkpoint at current progress using unified storage
                    i2p::core::TransferCheckpoint checkpoint;
                    checkpoint.serverB32 = serverB32;
                    checkpoint.filename = filename;
                    checkpoint.totalSize = metadata.totalSize;
                    checkpoint.bytesReceived = result.storage->size();
                    checkpoint.storagePath = result.storage->getStoragePath();
                    
                    // For large files, save storage path instead of data
                    if (metadata.totalSize > LARGE_FILE_THRESHOLD) {
                        checkpoint.partialData.clear(); // Don't save large data in checkpoint
                    } else {
                        checkpoint.partialData = result.getData(); // Use helper method
                    }
                    
                    checkpoint.lastUpdate = std::chrono::steady_clock::now();
                    checkpoint.attemptCount = 1;
                    
                    m_recoveryGuard->saveCheckpoint(checkpoint);
                    
                    // Trigger recovery through manual failure simulation
                    auto& recovery = i2p::core::TransferRecovery::getInstance();
                    i2p::core::StreamHealth health;
                    health.resendCount = 8; // Simulate high resend count
                    health.isStable = false;
                    
                    recovery.handleStreamFailure({0, 0}, health);
                }
                
                result.error = "Failed to receive chunk header " + std::to_string(chunkIndex) + 
                              " - got " + std::to_string(header.size()) + "/8 bytes";
                FT_LOG_ERROR("ChunkedFileClient", "Failed to receive chunk header " << chunkIndex);
                return;
            }
            
            // Parse header (little-endian)
            uint32_t receivedIndex = header[0] | (header[1] << 8) | (header[2] << 16) | (header[3] << 24);
            uint32_t chunkSize = header[4] | (header[5] << 8) | (header[6] << 16) | (header[7] << 24);
            
            FT_LOG_DEBUG_IF_ENABLED("ChunkedFileClient", "Chunk header - index=" << receivedIndex << ", size=" << chunkSize);
            
            if (receivedIndex != chunkIndex) {
                result.error = "Chunk index mismatch - expected " + std::to_string(chunkIndex) + 
                              ", got " + std::to_string(receivedIndex);
                return;
            }
            
            if (chunkSize == 0 || chunkSize > 1024*1024) { // Sanity check
                result.error = "Invalid chunk size: " + std::to_string(chunkSize);
                return;
            }
            
            // Read chunk data
            auto chunkData = readChunkFromStream(stream, chunkSize, i2p::filetransfer::TransferConfig::getChunkTimeout());
            if (chunkData.size() != chunkSize) {
                result.error = "Failed to receive chunk " + std::to_string(chunkIndex) + 
                              " data - expected " + std::to_string(chunkSize) + 
                              " bytes, got " + std::to_string(chunkData.size());
                return;
            }
            
            // Write to unified storage
            try {
                result.storage->append(chunkData);
            } catch (const std::exception& e) {
                result.error = "Failed to write chunk " + std::to_string(chunkIndex) + " to storage: " + e.what();
                return;
            }
            
            // Also update legacy data for small files (backward compatibility)
            if (metadata.totalSize <= LARGE_FILE_THRESHOLD) {
                result.data.insert(result.data.end(), chunkData.begin(), chunkData.end());
            }
            
            auto chunkEnd = std::chrono::steady_clock::now();
            auto chunkTime = std::chrono::duration_cast<std::chrono::milliseconds>(chunkEnd - chunkStart);
            result.stats.chunkTimes.push_back(chunkTime);
            result.stats.chunksReceived++;
            
            FT_LOG_DEBUG_IF_ENABLED("ChunkedFileClient", "Chunk " << (chunkIndex + 1) << " received (" << chunkSize << " bytes)");
            
            // Universal flow control - helps prevent overwhelming the sender
            enforceUniversalFlowControl();
            
            chunkIndex++;
        }
        
        result.stats.chunksTotal = chunkIndex;
        result.stats.totalBytes = result.storage->size();
        
        // Step 3: Verify data integrity using unified storage
        FT_LOG_INFO("ChunkedFileClient", "Verifying data integrity...");
        result.stats.verified = result.storage->verify(metadata.sha256Checksum);
        
        if (!result.stats.verified) {
            result.error = "Data verification failed";
            return;
        }
        
        result.success = true;
        result.stats.endTime = std::chrono::steady_clock::now();
        
        FT_LOG_INFO("ChunkedFileClient", "File received successfully - " << result.stats.totalBytes << " bytes verified at " << result.getStoragePath());
        
    } catch (const std::exception& e) {
        result.error = "Exception in receiveFileOnStream: " + std::string(e.what());
        FT_LOG_ERROR("ChunkedFileClient", "receiveFileOnStream exception: " << e.what());
    }
}

std::string ChunkedFileClient::readMessageFromStream(std::shared_ptr<stream::Stream> stream, int timeout_ms)
{
    // For the new protocol, metadata is sent as a complete message first
    // Format: "2:filename=...&size=...&checksum=..."
    
    std::vector<uint8_t> buffer(1024);  // Should be enough for metadata
    size_t bytesRead = stream->Receive(buffer.data(), buffer.size(), timeout_ms);
    
    if (bytesRead > 0) {
        std::string message(buffer.begin(), buffer.begin() + bytesRead);
        FT_LOG_INFO("ChunkedFileClient", "Read metadata message: " << message);
        return message;
    }
    
    FT_LOG_WARNING("ChunkedFileClient", "Failed to read metadata message from stream");
    return "";
}

std::vector<uint8_t> ChunkedFileClient::readChunkFromStream(std::shared_ptr<stream::Stream> stream, size_t expectedSize, int timeout_ms)
{
    std::vector<uint8_t> buffer(expectedSize);
    size_t totalBytesRead = 0;
    
    // Use short-timeout Receive() calls to process chunks as they arrive
    // This maintains packet ordering while avoiding long buffering delays
    auto startTime = std::chrono::steady_clock::now();
    const int CHUNK_TIMEOUT = 1000; // 1 second timeout for responsive chunk processing
    int maxAttempts = timeout_ms / CHUNK_TIMEOUT;
    int attempts = 0;
    
    while (totalBytesRead < expectedSize && attempts < maxAttempts) {
        size_t remaining = expectedSize - totalBytesRead;
        
        // Use short-timeout Receive() for incremental reading
        size_t bytesRead = stream->Receive(buffer.data() + totalBytesRead, remaining, CHUNK_TIMEOUT);
        
        if (bytesRead > 0) {
            totalBytesRead += bytesRead;
            FT_LOG_DEBUG_IF_ENABLED("ChunkedFileClient", "Receive: " << bytesRead << " bytes (" << totalBytesRead << "/" << expectedSize << ")");
            attempts = 0; // Reset attempts on successful read
        } else {
            attempts++;
            // Brief progress update without breaking immediately
            if (attempts % 5 == 0) { // Every 5 seconds
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime).count();
                FT_LOG_DEBUG_IF_ENABLED("ChunkedFileClient", "Waiting for data: " 
                    << totalBytesRead << "/" << expectedSize << " bytes after " << elapsed << "ms");
            }
        }
    }
    
    if (totalBytesRead == expectedSize) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        FT_LOG_DEBUG_IF_ENABLED("ChunkedFileClient", "Receive success: " << totalBytesRead << " bytes in " << elapsed << "ms");
        return buffer;
    } else {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        FT_LOG_WARNING("ChunkedFileClient", "Receive partial read - expected " << expectedSize 
            << ", got " << totalBytesRead << " bytes after " << elapsed << "ms (" << attempts << " timeouts)");
        buffer.resize(totalBytesRead);
        return buffer;
    }
}

FileMetadata ChunkedFileClient::parseMetadata(const std::string& payload)
{
    FileMetadata metadata;
    
    // Parse key=value&key=value format
    std::istringstream iss(payload);
    std::string token;
    while (std::getline(iss, token, '&')) {
        size_t pos = token.find('=');
        if (pos != std::string::npos) {
            std::string key = token.substr(0, pos);
            std::string value = token.substr(pos + 1);
            
            if (key == "filename") metadata.filename = value;
            else if (key == "size") metadata.totalSize = std::stoul(value);
            else if (key == "checksum") metadata.sha256Checksum = value;
            // Note: chunk_size and chunk_count are no longer in metadata - each chunk has its own header
        }
    }
    
    FT_LOG_DEBUG_IF_ENABLED("ChunkedFileClient", "Parsed metadata - filename=" << metadata.filename << ", size=" << metadata.totalSize << " (chunks will be discovered)");
    
    return metadata;
}

std::string ChunkedFileClient::sendMessage(embed::SimpleStreamClient* client,
                                          const std::string& serverB32,
                                          MessageType type, 
                                          const std::string& payload,
                                          int timeout_ms)
{
    try {
        // Simple format: TYPE:payload (SimpleSend/SimpleReceive provides reliability)
        std::string message = std::to_string(static_cast<int>(type)) + ":" + payload;
        std::string response = client->sendMessage(serverB32, message, timeout_ms);
        
        if (response.empty()) {
            FT_LOG_WARNING("ChunkedFileClient", "Empty response from server");
            return "";
        }
        
        // Parse simple TYPE:payload format
        // Handle potential "V" prefix from SimpleSend/SimpleReceive protocol
        size_t startPos = 0;
        if (response.size() > 0 && response[0] == 'V') {
            FT_LOG_DEBUG_IF_ENABLED("ChunkedFileClient", "Stripping 'V' prefix from response");
            startPos = 1;
        }
        
        size_t colonPos = response.find(':', startPos);
        if (colonPos == std::string::npos) {
            LogPrint(eLogWarning, "ChunkedFileClient: Invalid response format: ", response);
            return "";
        }
        
        std::string responsePayload = response.substr(colonPos + 1);
        FT_LOG_DEBUG_IF_ENABLED("ChunkedFileClient", "Extracted payload: [" << responsePayload << "]");
        return responsePayload;
        
    } catch (const std::exception& e) {
        FT_LOG_ERROR("ChunkedFileClient", "sendMessage exception: " << e.what());
        return "";
    }
}


bool ChunkedFileClient::verifyData(const std::vector<uint8_t>& data, const FileMetadata& metadata) const
{
    if (data.size() != metadata.totalSize) {
        FT_LOG_ERROR("ChunkedFileClient", "Size mismatch - expected " << metadata.totalSize << ", got " << data.size());
        return false;
    }
    
    std::string actualChecksum = ProtocolUtils::calculateSHA256(data);
    
    if (actualChecksum != metadata.sha256Checksum) {
        FT_LOG_ERROR("ChunkedFileClient", "Checksum mismatch - expected " << metadata.sha256Checksum << ", got " << actualChecksum);
        return false;
    }
    
    FT_LOG_INFO("ChunkedFileClient", "Data verification successful");
    return true;
}

void ChunkedFileClient::enforceUniversalFlowControl()
{
    // Use the universal flow control system that adapts to network conditions
    UniversalFlowControl::EnforceFlowControl();
}

bool ChunkedFileClient::handleRecovery(const i2p::core::TransferCheckpoint& checkpoint, 
                                      i2p::core::RecoveryStrategy strategy) {
    if (m_recoveryInProgress.exchange(true)) {
        LogPrint(eLogWarning, "ChunkedFileClient: Recovery already in progress");
        return false;
    }
    
    LogPrint(eLogInfo, "ChunkedFileClient: Handling recovery for ", checkpoint.filename, 
             " using strategy ", (int)strategy);
    
    bool success = false;
    
    try {
        switch (strategy) {
            case i2p::core::RecoveryStrategy::IMMEDIATE_RETRY:
                success = requestFile(checkpoint.serverB32, checkpoint.filename, 30000).success;
                break;
                
            case i2p::core::RecoveryStrategy::DELAYED_RETRY:
                std::this_thread::sleep_for(std::chrono::seconds(5));
                success = requestFile(checkpoint.serverB32, checkpoint.filename, 30000).success;
                break;
                
            case i2p::core::RecoveryStrategy::CHECKPOINT_RESUME:
                success = resumeTransfer(checkpoint, 30000).success;
                break;
                
            default:
                LogPrint(eLogWarning, "ChunkedFileClient: Unsupported recovery strategy");
                break;
        }
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileClient: Recovery exception: ", e.what());
    }
    
    m_recoveryInProgress = false;
    return success;
}

ChunkedFileClient::TransferResult ChunkedFileClient::resumeTransfer(
    const i2p::core::TransferCheckpoint& checkpoint, int timeout_ms) {
    
    TransferResult result;
    result.success = false;
    
    if (!checkpoint.isValid()) {
        result.error = "Invalid checkpoint";
        return result;
    }
    
    LogPrint(eLogInfo, "ChunkedFileClient: Resuming chunked transfer from ", 
             checkpoint.bytesReceived, "/", checkpoint.totalSize, " bytes");
    
    // For chunked protocol, implement actual resume by chunk index
    // TODO: Enhance protocol to support resume from specific chunk
    LogPrint(eLogInfo, "ChunkedFileClient: Chunked protocol restart required for now");
    
    return requestFile(checkpoint.serverB32, checkpoint.filename, timeout_ms);
}

bool ChunkedFileClient::validatePartialData(const std::vector<uint8_t>& data, 
                                          size_t expectedSize) const {
    if (data.empty()) {
        LogPrint(eLogWarning, "ChunkedFileClient: No data to validate");
        return false;
    }
    
    if (data.size() > expectedSize) {
        LogPrint(eLogError, "ChunkedFileClient: Data size exceeds expected - got ", 
                 data.size(), " expected ", expectedSize);
        return false;
    }
    
    // Check for obvious corruption patterns in chunks
    bool hasNonZero = false;
    for (size_t i = 0; i < std::min(data.size(), size_t(1024)); ++i) {
        if (data[i] != 0) {
            hasNonZero = true;
            break;
        }
    }
    
    if (!hasNonZero) {
        LogPrint(eLogWarning, "ChunkedFileClient: Data appears to be all zeros");
        return false;
    }
    
    LogPrint(eLogInfo, "ChunkedFileClient: Partial data validation passed - ", 
             data.size(), " bytes");
    return true;
}

} // namespace i2p::filetransfer