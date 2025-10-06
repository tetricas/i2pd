#include "NormalStreamingFileServer.h"
#include "FileTransferProtocol.h"
#include "../core/FileTransferLogging.h"
#include "../core/TransferConfig.h"
#include "Log.h"
#include "Identity.h"
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>

namespace i2p::filetransfer
{

NormalStreamingFileServer::NormalStreamingFileServer(std::shared_ptr<client::ClientDestination> destination)
    : m_destination(destination)
{
    if (!m_destination) {
        m_lastStatus = "Invalid destination provided";
        FT_LOG_ERROR("NormalStreamingFileServer", "Invalid destination");
        return;
    }
    
    m_lastStatus = "Server created, ready to start";
    FT_LOG_INFO("NormalStreamingFileServer", "Server created successfully");
}

void NormalStreamingFileServer::addMockFile(const std::string& filename, const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lock(m_filesMutex);
    
    // Store file data
    m_files[filename] = data;
    
    // Create binary metadata with optimized chunk size for normal streaming
    const size_t OPTIMAL_CHUNK_SIZE = 64 * 1024; // 64KB chunks for better performance
    auto metadata = BinaryProtocolUtils::createMetadata(filename, data, OPTIMAL_CHUNK_SIZE);
    m_fileMetadata[filename] = metadata;
    
    LogPrint(eLogInfo, "NormalStreamingFileServer: Added file '", filename, "' (", data.size(), 
             " bytes, ", static_cast<uint32_t>(metadata.chunkCount), " chunks)");
}

void NormalStreamingFileServer::generateMockFile(const std::string& filename, size_t size, const std::string& seed)
{
    std::string actualSeed = seed.empty() ? filename : seed;
    auto data = ProtocolUtils::generateMockFile(size, actualSeed);
    addMockFile(filename, data);
}

void NormalStreamingFileServer::start()
{
    // Wait for destination to be ready (give tunnels time to establish)
    if (!m_destination || !m_destination->IsReady()) {
        LogPrint(eLogInfo, "NormalStreamingFileServer: Waiting for destination to be ready...");
        int maxChecks = i2p::filetransfer::TransferConfig::getConnectionTimeout() / i2p::filetransfer::TransferConfig::getRetryDelayMs();
        for (int i = 0; i < maxChecks && (!m_destination || !m_destination->IsReady()); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(i2p::filetransfer::TransferConfig::getRetryDelayMs()));
        }
    }
    
    if (!m_destination || !m_destination->IsReady()) {
        m_lastStatus = "Destination not ready after timeout";
        FT_LOG_ERROR("NormalStreamingFileServer", "Destination not ready after timeout");
        return;
    }
    
    try {
        // Create Normal Streaming server (like ChunkedFileServer does)
        m_streamServer = std::make_unique<embed::NormalStreamServer>(m_destination);
        
        if (!m_streamServer) {
            m_lastStatus = "Failed to create stream server";
            FT_LOG_ERROR("NormalStreamingFileServer", "Failed to create stream server");
            return;
        }
        
        // Start normal streaming server with file transfer message handler
        m_streamServer->start([this](const std::string& message, const i2p::data::IdentHash& clientHash) -> std::string {
            return handleFileTransferMessage(message);
        });
        
        m_lastStatus = "Server started successfully";
        FT_LOG_INFO("NormalStreamingFileServer", "Server started on " << getB32Address());
        FT_LOG_INFO("NormalStreamingFileServer", "Serving " << m_files.size() << " files");
        
        // Log available files
        for (const auto& [filename, metadata] : m_fileMetadata) {
            LogPrint(eLogInfo, "NormalStreamingFileServer: - ", filename, " (", metadata.totalSize, " bytes)");
        }
        
    } catch (const std::exception& e) {
        m_lastStatus = "Start failed: " + std::string(e.what());
        FT_LOG_ERROR("NormalStreamingFileServer", "Start exception: " << e.what());
    }
}

void NormalStreamingFileServer::stop()
{
    if (m_streamServer) {
        m_streamServer->stop();
        m_lastStatus = "Server stopped";
        FT_LOG_INFO("NormalStreamingFileServer", "Server stopped");
    }
}

std::string NormalStreamingFileServer::getB32Address() const
{
    return m_streamServer ? m_streamServer->getB32Address() : "";
}

bool NormalStreamingFileServer::isReady() const
{
    // Before start(): just check if destination exists and is ready
    if (!m_streamServer) {
        return m_destination && m_destination->IsReady();
    }
    
    // After start(): check both destination and server
    return m_destination && 
           m_destination->IsReady() &&
           m_streamServer && 
           m_streamServer->isReady();
}

std::string NormalStreamingFileServer::getStatus() const
{
    if (!m_streamServer) return "Stream server not initialized";
    if (!m_destination) return "Destination not available";
    
    return "Running: " + std::string(isReady() ? "yes" : "no") + 
           ", Destination ready: " + std::string(m_destination ? "yes" : "no") + 
           ", Files: " + std::to_string(m_files.size()) + 
           ", " + m_lastStatus;
}

std::vector<std::string> NormalStreamingFileServer::getFileList() const
{
    std::lock_guard<std::mutex> lock(m_filesMutex);
    
    std::vector<std::string> fileList;
    fileList.reserve(m_files.size());
    
    for (const auto& [filename, _] : m_files) {
        fileList.push_back(filename);
    }
    
    return fileList;
}

std::string NormalStreamingFileServer::handleFileTransferMessage(const std::string& message)
{
    try {
        LogPrint(eLogInfo, "NormalStreamingFileServer: Handling message (", message.size(), " bytes)");
        
        // Check if this is a binary file request
        if (message.size() >= sizeof(BinaryFileRequest)) {
            auto filename = BinaryProtocolUtils::parseFileRequest(
                reinterpret_cast<const uint8_t*>(message.data()), message.size());
            
            if (!filename.empty()) {
                LogPrint(eLogInfo, "NormalStreamingFileServer: Binary file request for: ", filename);
                return handleBinaryFileRequest(filename);
            }
        }
        
        // Check if this is a continuous stream file request
        if (message.substr(0, 12) == "STREAM_FILE:") {
            std::string filename = message.substr(12);
            LogPrint(eLogInfo, "NormalStreamingFileServer: Continuous stream request for file: ", filename);
            return handleStreamFileRequest(filename);
        }
        
        LogPrint(eLogWarning, "NormalStreamingFileServer: Unknown message format");
        return "ERROR: Unknown message format";
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "NormalStreamingFileServer: Message handling exception: ", e.what());
        return "ERROR: " + std::string(e.what());
    }
}

std::string NormalStreamingFileServer::handleBinaryFileRequest(const std::string& filename)
{
    auto metadata = getFileMetadata(filename);
    
    if (metadata.totalSize == 0) {
        LogPrint(eLogError, "NormalStreamingFileServer: File not found: ", filename);
        return "ERROR: File not found";
    }
    
    // Return binary metadata
    auto serialized = BinaryProtocolUtils::serializeMetadata(metadata);
    std::string response(reinterpret_cast<const char*>(serialized.data()), serialized.size());
    
    LogPrint(eLogInfo, "NormalStreamingFileServer: Sent metadata for ", filename, " (", static_cast<uint64_t>(metadata.totalSize), " bytes)");
    return response;
}

std::string NormalStreamingFileServer::handleStreamFileRequest(const std::string& filename)
{
    auto fileData = getFileData(filename);
    
    if (fileData.empty()) {
        LogPrint(eLogError, "NormalStreamingFileServer: File not available: ", filename);
        return "ERROR: File not available";
    }
    
    LogPrint(eLogInfo, "NormalStreamingFileServer: Streaming entire file ", filename, 
             " (", fileData.size() / (1024*1024), " MB)");
    
    // Return entire file in one response - true streaming
    std::string response(reinterpret_cast<const char*>(fileData.data()), fileData.size());
    
    LogPrint(eLogInfo, "NormalStreamingFileServer: Sent complete file ", filename, 
             " (", fileData.size() / (1024*1024), " MB)");
    
    return response;
}

std::vector<uint8_t> NormalStreamingFileServer::getFileData(const std::string& filename) const
{
    std::lock_guard<std::mutex> lock(m_filesMutex);
    
    auto it = m_files.find(filename);
    if (it != m_files.end()) {
        return it->second;
    }
    
    return {};
}

BinaryFileMetadata NormalStreamingFileServer::getFileMetadata(const std::string& filename) const
{
    std::lock_guard<std::mutex> lock(m_filesMutex);
    
    auto it = m_fileMetadata.find(filename);
    if (it != m_fileMetadata.end()) {
        return it->second;
    }
    
    return {}; // Empty metadata indicates file not found
}

} // namespace i2p::filetransfer