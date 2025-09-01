#include "ChunkedFileClient.h"
#include "I2PdUtils.h"
#include "SimpleStreamingImpl.h"
#include "Log.h"
#include <sstream>
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
                    LogPrint(eLogInfo, "ChunkedFileClient: Accepting incoming stream from server");
                    handleIncomingStream(stream);
                }
            });
            LogPrint(eLogInfo, "ChunkedFileClient: Stream acceptor configured");
        } else {
            LogPrint(eLogWarning, "ChunkedFileClient: Could not get streaming destination");
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
    
    m_transferActive.store(true);
    // Note: startTime will be set when actual data reception begins
    
    try {
        LogPrint(eLogInfo, "ChunkedFileClient: Starting file transfer for: ", filename, " using new stream architecture");
        result.stats.initTime = std::chrono::steady_clock::now();

        // Step 1: Send file request via SimpleSend (lightweight)
        auto streamingDest = m_destination->GetStreamingDestination();
        if (!streamingDest) {
            result.error = "Could not get streaming destination";
            m_lastStatus = result.error;
            return result;
        }
        
        // Parse server address
        auto serverHash = embed::I2PdUtils::parseBase32(serverB32);
        
        // Request destination and wait for server's LeaseSet to be available
        LogPrint(eLogInfo, "ChunkedFileClient: Requesting server destination...");
        m_destination->RequestDestination(serverHash);
        LogPrint(eLogInfo, "ChunkedFileClient: Waiting for server LeaseSet...");
        embed::I2PdUtils::waitForLeaseSet(serverHash, timeout_ms / 2);
        
        // Create stream using the same method as SimpleStreamClient
        auto requestStream = m_destination->CreateStream(serverHash);
        for (int i = 0; i < 50 && !requestStream; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            requestStream = m_destination->CreateStream(serverHash);
        }
        
        if (!requestStream) {
            result.error = "Failed to create request stream";
            m_lastStatus = result.error;
            return result;
        }
        
        // Send request via SimpleSend
        result.stats.requestTime = std::chrono::steady_clock::now();
        std::string request = "1:filename=" + filename;
        LogPrint(eLogInfo, "ChunkedFileClient: Sending request: ", request);
        requestStream->SimpleSend(request, true); // Expect response to trigger custom handler
        
        // Step 2: Wait for server to establish response stream
        {
            std::unique_lock<std::mutex> lock(m_incomingStreamsMutex);
            m_expectingResponse = true;
            m_currentResponseStream = nullptr;
            
            LogPrint(eLogInfo, "ChunkedFileClient: Waiting for server response stream...");
            
            if (!m_streamCondition.wait_for(lock, std::chrono::milliseconds(timeout_ms / 2), 
                [this] { return m_currentResponseStream != nullptr; })) {
                result.error = "Timeout waiting for server response stream";
                m_lastStatus = result.error;
                return result;
            }
            
            LogPrint(eLogInfo, "ChunkedFileClient: Server response stream established");
        }
        
        // Step 3: Receive complete file on server's dedicated stream
        receiveFileOnStream(m_currentResponseStream, filename, timeout_ms / 2, result);
        
        if (result.success) {
            result.stats.endTime = std::chrono::steady_clock::now();
            m_lastStatus = "Transfer completed successfully";
            
            LogPrint(eLogInfo, "ChunkedFileClient: Transfer complete - ", 
                     result.stats.totalBytes, " bytes in ", result.stats.getTransferTime().count(), "ms, ",
                     result.stats.getThroughputKBps(), " KB/s");
        }
        
    } catch (const std::exception& e) {
        result.error = "Exception: " + std::string(e.what());
        m_lastStatus = result.error;
        LogPrint(eLogError, "ChunkedFileClient: Exception: ", e.what());
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
    
    LogPrint(eLogInfo, "ChunkedFileClient: Incoming stream received from server");
    
    if (m_expectingResponse && !m_currentResponseStream) {
        m_currentResponseStream = stream;
        LogPrint(eLogInfo, "ChunkedFileClient: Stream assigned for file transfer");
        m_streamCondition.notify_one();
    } else {
        LogPrint(eLogWarning, "ChunkedFileClient: Unexpected incoming stream - expecting=", 
                 m_expectingResponse, ", current=", (m_currentResponseStream ? "set" : "null"));
    }
}

void ChunkedFileClient::receiveFileOnStream(
    std::shared_ptr<stream::Stream> stream,
    const std::string& filename,
    int timeout_ms,
    TransferResult& result)
{
    if (!stream) {
        result.error = "No stream provided";
        return;
    }
    
    try {
        result.stats.startTime = std::chrono::steady_clock::now();
        LogPrint(eLogInfo, "ChunkedFileClient: Starting file reception on stream");
        
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
        
        LogPrint(eLogInfo, "ChunkedFileClient: Metadata received - Size: ", metadata.totalSize,
                 ", Checksum: ", metadata.sha256Checksum.substr(0, 8), "... (chunks will be discovered)");
        
        result.data.reserve(metadata.totalSize);
        
        // Step 2: Read chunks until we have all the data
        size_t chunkIndex = 0;
        while (result.data.size() < metadata.totalSize) {
            if (!m_transferActive.load()) {
                result.error = "Transfer cancelled";
                return;
            }
            
            auto chunkStart = std::chrono::steady_clock::now();
            
            LogPrint(eLogInfo, "ChunkedFileClient: Reading chunk header for chunk ", chunkIndex, 
                     " (received ", result.data.size(), "/", metadata.totalSize, " bytes so far)");
            
            // Read chunk header: [4 bytes chunk_index][4 bytes chunk_size]
            std::vector<uint8_t> header = readChunkFromStream(stream, 8, 15000);
            if (header.size() != 8) {
                result.error = "Failed to receive chunk header " + std::to_string(chunkIndex) + 
                              " - got " + std::to_string(header.size()) + "/8 bytes";
                LogPrint(eLogError, "ChunkedFileClient: Failed to receive chunk header ", chunkIndex);
                return;
            }
            
            // Parse header (little-endian)
            uint32_t receivedIndex = header[0] | (header[1] << 8) | (header[2] << 16) | (header[3] << 24);
            uint32_t chunkSize = header[4] | (header[5] << 8) | (header[6] << 16) | (header[7] << 24);
            
            LogPrint(eLogInfo, "ChunkedFileClient: Chunk header - index=", receivedIndex, ", size=", chunkSize);
            
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
            auto chunkData = readChunkFromStream(stream, chunkSize, 15000);
            if (chunkData.size() != chunkSize) {
                result.error = "Failed to receive chunk " + std::to_string(chunkIndex) + 
                              " data - expected " + std::to_string(chunkSize) + 
                              " bytes, got " + std::to_string(chunkData.size());
                return;
            }
            
            result.data.insert(result.data.end(), chunkData.begin(), chunkData.end());
            
            auto chunkEnd = std::chrono::steady_clock::now();
            auto chunkTime = std::chrono::duration_cast<std::chrono::milliseconds>(chunkEnd - chunkStart);
            result.stats.chunkTimes.push_back(chunkTime);
            result.stats.chunksReceived++;
            
            LogPrint(eLogInfo, "ChunkedFileClient: Chunk ", chunkIndex + 1, " received (", chunkSize, " bytes)");
            
            chunkIndex++;
        }
        
        result.stats.chunksTotal = chunkIndex;
        
        result.stats.totalBytes = result.data.size();
        
        // Step 3: Verify data integrity
        LogPrint(eLogInfo, "ChunkedFileClient: Verifying data integrity...");
        result.stats.verified = verifyData(result.data, metadata);
        
        if (!result.stats.verified) {
            result.error = "Data verification failed";
            return;
        }
        
        result.success = true;
        result.stats.endTime = std::chrono::steady_clock::now();
        
        LogPrint(eLogInfo, "ChunkedFileClient: File received successfully - ", 
                 result.stats.totalBytes, " bytes verified");
        
    } catch (const std::exception& e) {
        result.error = "Exception in receiveFileOnStream: " + std::string(e.what());
        LogPrint(eLogError, "ChunkedFileClient: receiveFileOnStream exception: ", e.what());
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
        LogPrint(eLogInfo, "ChunkedFileClient: Read metadata message: ", message);
        return message;
    }
    
    LogPrint(eLogWarning, "ChunkedFileClient: Failed to read metadata message from stream");
    return "";
}

std::vector<uint8_t> ChunkedFileClient::readChunkFromStream(std::shared_ptr<stream::Stream> stream, size_t expectedSize, int timeout_ms)
{
    std::vector<uint8_t> buffer(expectedSize);
    size_t totalBytesRead = 0;
    
    // Read in a loop until we get all expected bytes or timeout
    int remainingTimeout = timeout_ms;
    auto startTime = std::chrono::steady_clock::now();
    
    while (totalBytesRead < expectedSize && remainingTimeout > 0) {
        size_t bytesRead = stream->Receive(buffer.data() + totalBytesRead, 
                                         expectedSize - totalBytesRead, 
                                         std::min(remainingTimeout, 1000)); // Max 1 second per read
        
        if (bytesRead > 0) {
            totalBytesRead += bytesRead;
            LogPrint(eLogInfo, "ChunkedFileClient: Read ", bytesRead, " bytes (", 
                     totalBytesRead, "/", expectedSize, ")");
        } else {
            // No data received, check if we should continue waiting
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            remainingTimeout = timeout_ms - static_cast<int>(elapsed);
            
            if (remainingTimeout <= 0) {
                LogPrint(eLogWarning, "ChunkedFileClient: Timeout reading chunk data, got ", 
                         totalBytesRead, "/", expectedSize, " bytes after ", elapsed, "ms");
                break;
            }
            
            // Brief pause before retry, but be more persistent
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            LogPrint(eLogDebug, "ChunkedFileClient: Waiting for more data, ", 
                     totalBytesRead, "/", expectedSize, " bytes, ", remainingTimeout, "ms remaining");
        }
    }
    
    if (totalBytesRead == expectedSize) {
        LogPrint(eLogDebug, "ChunkedFileClient: Successfully read complete chunk: ", totalBytesRead, " bytes");
        return buffer;
    } else {
        LogPrint(eLogError, "ChunkedFileClient: Failed to read complete chunk - expected ", 
                 expectedSize, ", got ", totalBytesRead, " bytes");
        buffer.resize(totalBytesRead); // Return partial data
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
    
    LogPrint(eLogDebug, "ChunkedFileClient: Parsed metadata - filename=", metadata.filename,
             ", size=", metadata.totalSize, " (chunks will be discovered)");
    
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
            LogPrint(eLogWarning, "ChunkedFileClient: Empty response from server");
            return "";
        }
        
        // Parse simple TYPE:payload format
        // Handle potential "V" prefix from SimpleSend/SimpleReceive protocol
        size_t startPos = 0;
        if (response.size() > 0 && response[0] == 'V') {
            LogPrint(eLogDebug, "ChunkedFileClient: Stripping 'V' prefix from response");
            startPos = 1;
        }
        
        size_t colonPos = response.find(':', startPos);
        if (colonPos == std::string::npos) {
            LogPrint(eLogWarning, "ChunkedFileClient: Invalid response format: ", response);
            return "";
        }
        
        std::string responsePayload = response.substr(colonPos + 1);
        LogPrint(eLogDebug, "ChunkedFileClient: Extracted payload: [", responsePayload, "]");
        return responsePayload;
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileClient: sendMessage exception: ", e.what());
        return "";
    }
}


bool ChunkedFileClient::verifyData(const std::vector<uint8_t>& data, const FileMetadata& metadata) const
{
    if (data.size() != metadata.totalSize) {
        LogPrint(eLogError, "ChunkedFileClient: Size mismatch - expected ", metadata.totalSize, ", got ", data.size());
        return false;
    }
    
    std::string actualChecksum = ProtocolUtils::calculateSHA256(data);
    
    if (actualChecksum != metadata.sha256Checksum) {
        LogPrint(eLogError, "ChunkedFileClient: Checksum mismatch - expected ", metadata.sha256Checksum, ", got ", actualChecksum);
        return false;
    }
    
    LogPrint(eLogInfo, "ChunkedFileClient: Data verification successful");
    return true;
}

} // namespace i2p::filetransfer