#include "NormalStreamingFileClient.h"
#include "FileTransferProtocol.h"
#include "Log.h"
#include <algorithm>
#include <thread>
#include <chrono>

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
    
    LogPrint(eLogInfo, "NormalStreamingFileClient: Starting download - ", filename);
    
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
        
        // Step 2: Download file using streaming chunks
        result.stats.startTime = std::chrono::steady_clock::now();
        auto fileData = downloadFileStream(serverB32, metadata, (timeout_ms * 2) / 3);
        result.stats.endTime = std::chrono::steady_clock::now();
        
        // Step 3: Verify data integrity
        if (verifyDownloadedData(fileData, metadata)) {
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
            result.error = "Data verification failed";
            LogPrint(eLogError, "NormalStreamingFileClient: Data verification failed");
        }
        
    } catch (const std::exception& e) {
        result.error = "Download exception: " + std::string(e.what());
        LogPrint(eLogError, "NormalStreamingFileClient: Exception: ", e.what());
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

} // namespace i2p::filetransfer