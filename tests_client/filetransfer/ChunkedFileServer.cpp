#include "ChunkedFileServer.h"
#include "../transport/SimpleStreamingImpl.h"
#include "../core/I2PdUtils.h"
#include "../core/FileTransferLogging.h"
#include "../core/TransferConfig.h"
#include "../core/UniversalFlowControl.h"
#include "Log.h"
#include <sstream>
#include <algorithm>
#include <thread>

namespace i2p::filetransfer
{

ChunkedFileServer::ChunkedFileServer(std::shared_ptr<client::ClientDestination> destination)
    : m_destination(std::move(destination))
{
}

ChunkedFileServer::~ChunkedFileServer()
{
    stop();
}

void ChunkedFileServer::addMockFile(const std::string& filename, const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lock(m_filesMutex);
    
    m_files[filename] = data;
    
    // Create metadata
    FileMetadata metadata(filename, data.size());
    metadata.sha256Checksum = ProtocolUtils::calculateSHA256(data);
    m_fileMetadata[filename] = metadata;
    
    FT_LOG_INFO("ChunkedFileServer", "Added file '" << filename << "' (" << data.size() << " bytes, " << metadata.chunkCount << " chunks, checksum: " << metadata.sha256Checksum.substr(0, 8) << "...)");
}

void ChunkedFileServer::generateMockFile(const std::string& filename, size_t size, const std::string& seed)
{
    std::string actualSeed = seed.empty() ? filename : seed;
    
    // For large files (>100MB), use virtual file system for memory efficiency
    const size_t VIRTUAL_FILE_THRESHOLD = 100 * 1024 * 1024; // 100MB
    
    if (size > VIRTUAL_FILE_THRESHOLD) {
        LogPrint(eLogInfo, "ChunkedFileServer: Creating large virtual file: ", filename, " (", size, " bytes)");
        
        // Create virtual file
        auto virtualFile = std::make_shared<MockVirtualFile>(size, actualSeed);
        
        // Create metadata for virtual file
        FileMetadata metadata(filename, size);
        metadata.isVirtual = true;
        metadata.sha256Checksum = virtualFile->calculateChecksum();
        
        // Store virtual file and metadata
        std::lock_guard<std::mutex> lock(m_filesMutex);
        m_virtualFiles[filename] = virtualFile;
        m_fileMetadata[filename] = metadata;
        
        FT_LOG_INFO("ChunkedFileServer", "Added virtual file '" << filename << "' (" << size << " bytes, " 
                   << metadata.chunkCount << " chunks, checksum: " << metadata.sha256Checksum.substr(0, 8) << "...)");
    } else {
        // Use regular in-memory generation for smaller files
        auto data = ProtocolUtils::generateMockFile(size, actualSeed);
        addMockFile(filename, data);
    }
}

void ChunkedFileServer::start()
{
    if (m_running.load()) {
        FT_LOG_WARNING("ChunkedFileServer", "Already running");
        return;
    }
    
    if (!isReady()) {
        m_lastStatus = "Destination not ready";
        FT_LOG_ERROR("ChunkedFileServer", "Destination not ready");
        return;
    }
    
    try {
        // Create stream server
        m_server = std::make_unique<embed::SimpleStreamServer>(m_destination);
        
        if (!m_server) {
            m_lastStatus = "Failed to create stream server";
            FT_LOG_ERROR("ChunkedFileServer", "Failed to create stream server");
            return;
        }
        
        // Start server with message handler
        m_server->start([this](const std::string& message, const i2p::data::IdentHash& clientHash) -> std::string {
            return handleClientMessage(message, clientHash);
        });
        
        m_running.store(true);
        m_lastStatus = "Server started successfully";
        
        FT_LOG_INFO("ChunkedFileServer", "Server started on " << getB32Address());
        FT_LOG_INFO("ChunkedFileServer", "Serving " << m_files.size() << " files");
        
        // Log available files
        for (const auto& [filename, metadata] : m_fileMetadata) {
            LogPrint(eLogInfo, "ChunkedFileServer: - ", filename, " (", metadata.totalSize, " bytes)");
        }
        
    } catch (const std::exception& e) {
        m_lastStatus = "Start failed: " + std::string(e.what());
        FT_LOG_ERROR("ChunkedFileServer", "Start exception: " << e.what());
    }
}

void ChunkedFileServer::stop()
{
    if (m_running.load()) {
        m_running.store(false);
        
        if (m_server) {
            m_server->stop();
            m_server.reset();
        }
        
        m_lastStatus = "Server stopped";
        FT_LOG_INFO("ChunkedFileServer", "Server stopped");
    }
}

std::string ChunkedFileServer::getB32Address() const
{
    if (m_destination)
        return m_destination->GetIdentHash().ToBase32() + ".b32.i2p";
    return "";
}

bool ChunkedFileServer::isReady() const
{
    return m_destination && m_destination->IsReady();
}

std::string ChunkedFileServer::getStatus() const
{
    if (!m_destination)
        return "No destination";
        
    std::ostringstream oss;
    oss << "Running: " << (m_running.load() ? "yes" : "no")
        << ", Destination ready: " << (m_destination->IsReady() ? "yes" : "no")
        << ", Files: " << m_files.size();
    if (!m_lastStatus.empty())
        oss << ", Status: " << m_lastStatus;
        
    return oss.str();
}

std::vector<std::string> ChunkedFileServer::getFileList() const
{
    std::lock_guard<std::mutex> lock(m_filesMutex);
    std::vector<std::string> files;
    
    for (const auto& [filename, _] : m_files) {
        files.push_back(filename);
    }
    
    return files;
}

std::string ChunkedFileServer::handleClientMessage(const std::string& clientMessage, const i2p::data::IdentHash& clientHash)
{
    try {
        // Parse simple TYPE:payload format
        size_t colonPos = clientMessage.find(':');
        if (colonPos == std::string::npos) {
            FT_LOG_WARNING("ChunkedFileServer", "Invalid message format");
            return createErrorResponse(ErrorCode::INVALID_REQUEST, "Invalid message format");
        }
        
        int typeInt = std::stoi(clientMessage.substr(0, colonPos));
        MessageType messageType = static_cast<MessageType>(typeInt);
        std::string payload = clientMessage.substr(colonPos + 1);
        
        LogPrint(eLogDebug, "ChunkedFileServer: Received message type ", static_cast<int>(messageType),
                ", payload: [", payload.substr(0, 50), (payload.size() > 50 ? "...]" : "]"));
        
        switch (messageType) {
            case MessageType::FILE_REQUEST: {
                // New architecture: Handle file request asynchronously via stream
                std::string filename = parseFilenameFromPayload(payload);
                if (!filename.empty()) {
                    LogPrint(eLogInfo, "ChunkedFileServer: File request for: ", filename, " from client: ", clientHash.ToBase32().substr(0, 8), "... - starting stream transfer");
                    
                    // Start file transfer on dedicated stream in background thread
                    std::thread([this, filename, clientHash]() {
                        handleFileTransferOnStream(filename, clientHash);
                    }).detach();
                    
                    // Return immediate acknowledgment
                    return "0:request_received";
                } else {
                    return createErrorResponse(ErrorCode::INVALID_REQUEST, "Invalid filename in request");
                }
            }
            
            case MessageType::RESUME_REQUEST: {
                // Handle resume request
                return handleResumeRequest(payload, clientHash);
            }
                
            case MessageType::CHUNK_REQUEST:
                // Legacy support
                return handleChunkRequest(payload);
                
            case MessageType::TRANSFER_COMPLETE:
                return handleTransferComplete(payload);
                
            default:
                LogPrint(eLogWarning, "ChunkedFileServer: Unknown message type: ", static_cast<int>(messageType));
                return createErrorResponse(ErrorCode::INVALID_REQUEST, "Unknown message type");
        }
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: handleClientMessage exception: ", e.what());
        return createErrorResponse(ErrorCode::SERVER_ERROR, "Server error");
    }
}

std::string ChunkedFileServer::handleFileRequest(const std::string& payload)
{
    try {
        // Parse simple key=value format
        std::string filename;
        std::istringstream iss(payload);
        std::string token;
        while (std::getline(iss, token, '&')) {
            size_t pos = token.find('=');
            if (pos != std::string::npos) {
                std::string key = token.substr(0, pos);
                std::string value = token.substr(pos + 1);
                if (key == "filename") filename = value;
            }
        }
        
        LogPrint(eLogInfo, "ChunkedFileServer: File request for: ", filename);
        
        std::lock_guard<std::mutex> lock(m_filesMutex);
        
        auto it = m_fileMetadata.find(filename);
        if (it == m_fileMetadata.end()) {
            // Try to load file from disk if not in registry
            if (tryLoadFileFromDisk(filename)) {
                it = m_fileMetadata.find(filename);
            }
            
            if (it == m_fileMetadata.end()) {
                LogPrint(eLogWarning, "ChunkedFileServer: File not found: ", filename);
                return createErrorResponse(ErrorCode::FILE_NOT_FOUND, "File not found: " + filename);
            }
        }
        
        const auto& metadata = it->second;
        
        // Create metadata response
        std::ostringstream response;
        response << "filename=" << metadata.filename
                << "&size=" << metadata.totalSize
                << "&chunk_size=" << metadata.chunkSize
                << "&chunk_count=" << metadata.chunkCount
                << "&checksum=" << metadata.sha256Checksum;
        
        LogPrint(eLogInfo, "ChunkedFileServer: Sending metadata for ", filename, 
                 " (", metadata.totalSize, " bytes, ", metadata.chunkCount, " chunks)");
        
        return std::to_string(static_cast<int>(MessageType::METADATA_RESPONSE)) + ":" + response.str();
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: handleFileRequest exception: ", e.what());
        return createErrorResponse(ErrorCode::SERVER_ERROR, "Failed to process file request");
    }
}

std::string ChunkedFileServer::handleChunkRequest(const std::string& payload)
{
    try {
        // Parse simple key=value format
        std::string filename;
        size_t chunkIndex = 0;
        std::istringstream iss(payload);
        std::string token;
        while (std::getline(iss, token, '&')) {
            size_t pos = token.find('=');
            if (pos != std::string::npos) {
                std::string key = token.substr(0, pos);
                std::string value = token.substr(pos + 1);
                if (key == "filename") filename = value;
                else if (key == "chunk_index") chunkIndex = std::stoul(value);
            }
        }
        
        LogPrint(eLogDebug, "ChunkedFileServer: Chunk request for ", filename, ", chunk ", chunkIndex);
        
        std::lock_guard<std::mutex> lock(m_filesMutex);
        
        auto fileIt = m_files.find(filename);
        auto metaIt = m_fileMetadata.find(filename);
        
        if (fileIt == m_files.end() || metaIt == m_fileMetadata.end()) {
            LogPrint(eLogWarning, "ChunkedFileServer: File not found for chunk request: ", filename);
            return createErrorResponse(ErrorCode::FILE_NOT_FOUND, "File not found: " + filename);
        }
        
        const auto& fileData = fileIt->second;
        const auto& metadata = metaIt->second;
        
        if (chunkIndex >= metadata.chunkCount) {
            LogPrint(eLogWarning, "ChunkedFileServer: Chunk index out of range: ", chunkIndex, "/", metadata.chunkCount);
            return createErrorResponse(ErrorCode::CHUNK_OUT_OF_RANGE, "Chunk index out of range");
        }
        
        // Extract chunk data
        size_t startOffset = chunkIndex * metadata.chunkSize;
        size_t chunkSize = std::min(metadata.chunkSize, fileData.size() - startOffset);
        
        std::vector<uint8_t> chunkData(fileData.begin() + startOffset, 
                                      fileData.begin() + startOffset + chunkSize);
        
        LogPrint(eLogDebug, "ChunkedFileServer: Sending chunk ", chunkIndex, " (", chunkSize, " bytes)");
        
        // Return raw chunk data
        std::string chunkResponse(chunkData.begin(), chunkData.end());
        return std::to_string(static_cast<int>(MessageType::CHUNK_DATA)) + ":" + chunkResponse;
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: handleChunkRequest exception: ", e.what());
        return createErrorResponse(ErrorCode::SERVER_ERROR, "Failed to process chunk request");
    }
}

std::string ChunkedFileServer::handleResumeRequest(const std::string& payload, const i2p::data::IdentHash& clientHash)
{
    try {
        // Parse resume request: filename=test.bin&resume_offset=52428800&checksum_partial=abc123...
        std::string filename;
        size_t resumeOffset = 0;
        std::string partialChecksum;
        
        std::istringstream iss(payload);
        std::string token;
        while (std::getline(iss, token, '&')) {
            size_t pos = token.find('=');
            if (pos != std::string::npos) {
                std::string key = token.substr(0, pos);
                std::string value = token.substr(pos + 1);
                if (key == "filename") filename = value;
                else if (key == "resume_offset") resumeOffset = std::stoul(value);
                else if (key == "checksum_partial") partialChecksum = value;
            }
        }
        
        LogPrint(eLogInfo, "ChunkedFileServer: Resume request for: ", filename, 
                 " at offset: ", resumeOffset, " with checksum: ", partialChecksum.substr(0, 8), "...");
        
        // Check if file exists
        std::lock_guard<std::mutex> lock(m_filesMutex);
        auto fileIt = m_files.find(filename);
        auto metaIt = m_fileMetadata.find(filename);
        
        if (fileIt == m_files.end() || metaIt == m_fileMetadata.end()) {
            LogPrint(eLogWarning, "ChunkedFileServer: File not found for resume: ", filename);
            return createErrorResponse(ErrorCode::FILE_NOT_FOUND, "File not found: " + filename);
        }
        
        const auto& fileData = fileIt->second;
        const auto& metadata = metaIt->second;
        
        // Validate resume offset
        if (resumeOffset >= metadata.totalSize) {
            LogPrint(eLogWarning, "ChunkedFileServer: Invalid resume offset: ", resumeOffset, "/", metadata.totalSize);
            return createErrorResponse(ErrorCode::RESUME_INVALID_OFFSET, "Resume offset beyond file size");
        }
        
        // Verify partial checksum if provided
        if (!partialChecksum.empty() && resumeOffset > 0) {
            std::vector<uint8_t> partialData(fileData.begin(), fileData.begin() + resumeOffset);
            std::string calculatedChecksum = ProtocolUtils::calculateSHA256(partialData);
            
            if (calculatedChecksum != partialChecksum) {
                LogPrint(eLogWarning, "ChunkedFileServer: Partial checksum mismatch - expected: ", 
                         partialChecksum, " calculated: ", calculatedChecksum);
                return createErrorResponse(ErrorCode::CHECKSUM_MISMATCH, "Partial checksum verification failed");
            }
        }
        
        // Create resume response - confirm we can resume
        ResumeResponse resumeResp(true, resumeOffset);
        
        // Calculate checksum for remaining data
        if (resumeOffset < metadata.totalSize) {
            std::vector<uint8_t> remainingData(fileData.begin() + resumeOffset, fileData.end());
            resumeResp.remainingChecksum = ProtocolUtils::calculateSHA256(remainingData);
        }
        
        LogPrint(eLogInfo, "ChunkedFileServer: Resume approved - ", filename, 
                 " from offset ", resumeOffset, " (", metadata.totalSize - resumeOffset, " bytes remaining)");
        
        // Start resumed file transfer on dedicated stream
        std::thread([this, filename, clientHash, resumeOffset]() {
            handleResumedFileTransferOnStream(filename, clientHash, resumeOffset);
        }).detach();
        
        // Return immediate acknowledgment with resume confirmation
        std::ostringstream response;
        response << "can_resume=true&confirmed_offset=" << resumeResp.confirmedOffset
                << "&remaining_checksum=" << resumeResp.remainingChecksum;
        
        return std::to_string(static_cast<int>(MessageType::RESUME_RESPONSE)) + ":" + response.str();
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: handleResumeRequest exception: ", e.what());
        return createErrorResponse(ErrorCode::SERVER_ERROR, "Failed to process resume request");
    }
}

std::string ChunkedFileServer::handleTransferComplete(const std::string& payload)
{
    try {
        // Parse simple key=value format
        std::string filename = "unknown";
        std::string status = "unknown";
        size_t bytesReceived = 0;
        std::istringstream iss(payload);
        std::string token;
        while (std::getline(iss, token, '&')) {
            size_t pos = token.find('=');
            if (pos != std::string::npos) {
                std::string key = token.substr(0, pos);
                std::string value = token.substr(pos + 1);
                if (key == "filename") filename = value;
                else if (key == "status") status = value;
                else if (key == "bytes_received") bytesReceived = std::stoul(value);
            }
        }
        
        LogPrint(eLogInfo, "ChunkedFileServer: Transfer complete for ", filename, 
                 " - Status: ", status, ", Bytes: ", bytesReceived);
        
        // Simple acknowledgment
        std::ostringstream response;
        response << "acknowledged=true&filename=" << filename;
        
        return std::to_string(static_cast<int>(MessageType::TRANSFER_COMPLETE)) + ":" + response.str();
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: handleTransferComplete exception: ", e.what());
        return createErrorResponse(ErrorCode::SERVER_ERROR, "Failed to process transfer complete");
    }
}

std::string ChunkedFileServer::createErrorResponse(ErrorCode errorCode, const std::string& message)
{
    std::ostringstream errorResponse;
    errorResponse << "error_code=" << static_cast<int>(errorCode) << "&error_message=" << message;
    
    LogPrint(eLogWarning, "ChunkedFileServer: Error response - Code: ", static_cast<int>(errorCode), ", Message: ", message);
    
    return std::to_string(static_cast<int>(MessageType::ERROR_RESPONSE)) + ":" + errorResponse.str();
}

// New architecture methods

std::string ChunkedFileServer::parseFilenameFromPayload(const std::string& payload)
{
    // Parse key=value format to extract filename
    std::istringstream iss(payload);
    std::string token;
    while (std::getline(iss, token, '&')) {
        size_t pos = token.find('=');
        if (pos != std::string::npos) {
            std::string key = token.substr(0, pos);
            std::string value = token.substr(pos + 1);
            if (key == "filename") {
                return value;
            }
        }
    }
    return "";
}

void ChunkedFileServer::handleFileTransferOnStream(const std::string& filename, const i2p::data::IdentHash& clientHash)
{
    LogPrint(eLogInfo, "ChunkedFileServer: Starting file transfer on stream for: ", filename);
    
    try {
        // Create outbound stream to client
        auto streamingDest = m_destination->GetStreamingDestination();
        if (!streamingDest) {
            LogPrint(eLogError, "ChunkedFileServer: Could not get streaming destination");
            return;
        }
        
        // Wait a moment for client to set up stream acceptor
        FT_LOG_INFO("ChunkedFileServer", "Waiting for client to setup stream acceptor...");
        std::this_thread::sleep_for(std::chrono::milliseconds(i2p::filetransfer::TransferConfig::getConnectionTimeout() / 20));
        
        // Create stream to client directly using identity hash
        LogPrint(eLogInfo, "ChunkedFileServer: Creating stream to client using identity hash...");
        auto stream = m_destination->CreateStream(clientHash);
        for (int i = 0; i < 50 && !stream; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stream = m_destination->CreateStream(clientHash);
        }
        
        if (!stream) {
            LogPrint(eLogError, "ChunkedFileServer: Failed to create stream to client");
            return;
        }
        
        // Wait for stream to establish
        LogPrint(eLogInfo, "ChunkedFileServer: Waiting for stream establishment...");
        for (int i = 0; i < 50 && !stream->IsEstablished(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (!stream->IsEstablished()) {
            LogPrint(eLogWarning, "ChunkedFileServer: Stream not established, proceeding anyway");
        }
        
        LogPrint(eLogInfo, "ChunkedFileServer: Stream ready, sending file: ", filename);
        
        // Send complete file on stream
        if (sendFileOnStream(stream, filename)) {
            LogPrint(eLogInfo, "ChunkedFileServer: File transfer completed successfully");
        } else {
            LogPrint(eLogError, "ChunkedFileServer: File transfer failed");
        }
        
        // Close stream
        stream->Close();
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: Exception in handleFileTransferOnStream: ", e.what());
    }
}

void ChunkedFileServer::handleResumedFileTransferOnStream(const std::string& filename, const i2p::data::IdentHash& clientHash, size_t resumeOffset)
{
    LogPrint(eLogInfo, "ChunkedFileServer: Starting RESUMED file transfer on stream for: ", filename, " from offset: ", resumeOffset);
    
    try {
        // Create outbound stream to client
        auto streamingDest = m_destination->GetStreamingDestination();
        if (!streamingDest) {
            LogPrint(eLogError, "ChunkedFileServer: Could not get streaming destination for resume");
            return;
        }
        
        // Wait a moment for client to set up stream acceptor
        std::this_thread::sleep_for(std::chrono::milliseconds(i2p::filetransfer::TransferConfig::getConnectionTimeout() / 20));
        
        // Create stream to client
        auto stream = m_destination->CreateStream(clientHash);
        for (int i = 0; i < 50 && !stream; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stream = m_destination->CreateStream(clientHash);
        }
        
        if (!stream) {
            LogPrint(eLogError, "ChunkedFileServer: Failed to create stream to client for resume");
            return;
        }
        
        // Wait for stream to establish
        for (int i = 0; i < 50 && !stream->IsEstablished(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        LogPrint(eLogInfo, "ChunkedFileServer: Resume stream ready, sending remaining file data from offset: ", resumeOffset);
        
        // Send remaining file data from offset
        if (sendFileOnStreamFromOffset(stream, filename, resumeOffset)) {
            LogPrint(eLogInfo, "ChunkedFileServer: Resume transfer completed successfully");
        } else {
            LogPrint(eLogError, "ChunkedFileServer: Resume transfer failed");
        }
        
        // Close stream
        stream->Close();
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: Exception in handleResumedFileTransferOnStream: ", e.what());
    }
}

bool ChunkedFileServer::sendFileOnStream(std::shared_ptr<stream::Stream> stream, const std::string& filename)
{
    if (!stream) {
        LogPrint(eLogError, "ChunkedFileServer: No stream provided");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_filesMutex);
    
    // Check if file exists (in-memory or virtual)
    auto metaIt = m_fileMetadata.find(filename);
    if (metaIt == m_fileMetadata.end()) {
        LogPrint(eLogError, "ChunkedFileServer: File not found: ", filename);
        return false;
    }
    
    const auto& metadata = metaIt->second;
    
    // Check if it's a virtual file or regular file
    std::shared_ptr<VirtualFile> virtualFile = nullptr;
    std::vector<uint8_t> fileData;
    
    if (metadata.isVirtual) {
        auto virtualIt = m_virtualFiles.find(filename);
        if (virtualIt == m_virtualFiles.end()) {
            LogPrint(eLogError, "ChunkedFileServer: Virtual file not found: ", filename);
            return false;
        }
        virtualFile = virtualIt->second;
        LogPrint(eLogInfo, "ChunkedFileServer: Using virtual file for: ", filename);
    } else {
        auto fileIt = m_files.find(filename);
        if (fileIt == m_files.end()) {
            LogPrint(eLogError, "ChunkedFileServer: Regular file not found: ", filename);
            return false;
        }
        fileData = fileIt->second;
        LogPrint(eLogInfo, "ChunkedFileServer: Using regular file for: ", filename);
    }
    
    try {
        // Step 1: Send metadata (without chunk_count - let client discover end naturally)
        std::ostringstream metadataMsg;
        metadataMsg << "2:filename=" << metadata.filename
                   << "&size=" << metadata.totalSize
                   << "&checksum=" << metadata.sha256Checksum;
        
        LogPrint(eLogInfo, "ChunkedFileServer: Sending metadata: ", metadataMsg.str());
        
        size_t metadataSent = sendMessageOnStream(stream, metadataMsg.str());
        if (metadataSent == 0) {
            LogPrint(eLogError, "ChunkedFileServer: Failed to send metadata");
            return false;
        }
        
        LogPrint(eLogInfo, "ChunkedFileServer: Metadata sent (", metadataSent, " bytes), sending chunks with headers...");
        
        // Step 2: Send all chunks sequentially
        int lastThreshold = 0;
        for (size_t chunkIndex = 0; chunkIndex < metadata.chunkCount; ++chunkIndex) {
            if (!m_running.load()) {
                LogPrint(eLogInfo, "ChunkedFileServer: Transfer cancelled");
                return false;
            }
            
            // Extract chunk data
            size_t startOffset = chunkIndex * metadata.chunkSize;
            size_t chunkSize = std::min(metadata.chunkSize, metadata.totalSize - startOffset);
            
            // Create chunk with header: [4 bytes chunk_index][4 bytes chunk_size][chunk_data]
            std::vector<uint8_t> chunkPacket;
            chunkPacket.reserve(8 + chunkSize); // header + data
            
            // Add chunk index (4 bytes, little-endian)
            uint32_t idx = static_cast<uint32_t>(chunkIndex);
            chunkPacket.push_back(idx & 0xFF);
            chunkPacket.push_back((idx >> 8) & 0xFF);
            chunkPacket.push_back((idx >> 16) & 0xFF);
            chunkPacket.push_back((idx >> 24) & 0xFF);
            
            // Add chunk size (4 bytes, little-endian)
            uint32_t size = static_cast<uint32_t>(chunkSize);
            chunkPacket.push_back(size & 0xFF);
            chunkPacket.push_back((size >> 8) & 0xFF);
            chunkPacket.push_back((size >> 16) & 0xFF);
            chunkPacket.push_back((size >> 24) & 0xFF);
            
            // Add chunk data - generate on demand for virtual files or use stored data
            if (metadata.isVirtual && virtualFile) {
                // Generate chunk data on-demand
                auto chunkData = virtualFile->generateChunk(startOffset, chunkSize);
                chunkPacket.insert(chunkPacket.end(), chunkData.begin(), chunkData.end());
            } else {
                // Use pre-loaded file data
                chunkPacket.insert(chunkPacket.end(), 
                                  fileData.begin() + startOffset,
                                  fileData.begin() + startOffset + chunkSize);
            }
            
            size_t chunkSent = sendChunkOnStream(stream, chunkPacket);
            if (chunkSent != chunkPacket.size()) {
                LogPrint(eLogError, "ChunkedFileServer: Failed to send complete chunk ", chunkIndex, 
                         " - sent ", chunkSent, "/", chunkPacket.size(), " bytes");
                return false;
            }


            double progress = 100.0 * (chunkIndex + 1) / metadata.chunkCount;
            if (progress >= lastThreshold + 10)
            {
                lastThreshold += 10;
                LogPrint(eLogInfo, "ChunkedFileServer: Sent chunk ", chunkIndex + 1, "/",
                metadata.chunkCount, " (", chunkSize, " data bytes + 8 header bytes)");
                        std::cout << "Progress: " << lastThreshold << "%\n";
            }
        }
        
        LogPrint(eLogInfo, "ChunkedFileServer: All chunks sent successfully");
        return true;
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: Exception in sendFileOnStream: ", e.what());
        return false;
    }
}

bool ChunkedFileServer::sendFileOnStreamFromOffset(std::shared_ptr<stream::Stream> stream, const std::string& filename, size_t resumeOffset)
{
    if (!stream) {
        LogPrint(eLogError, "ChunkedFileServer: No stream provided for resume");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_filesMutex);
    
    // Check if file exists
    auto fileIt = m_files.find(filename);
    auto metaIt = m_fileMetadata.find(filename);
    
    if (fileIt == m_files.end() || metaIt == m_fileMetadata.end()) {
        LogPrint(eLogError, "ChunkedFileServer: File not found for resume: ", filename);
        return false;
    }
    
    const auto& fileData = fileIt->second;
    const auto& metadata = metaIt->second;
    
    if (resumeOffset >= metadata.totalSize) {
        LogPrint(eLogError, "ChunkedFileServer: Resume offset beyond file size: ", resumeOffset, "/", metadata.totalSize);
        return false;
    }
    
    try {
        // Step 1: Send metadata for remaining portion
        std::ostringstream metadataMsg;
        size_t remainingSize = metadata.totalSize - resumeOffset;
        
        // Calculate checksum for remaining data
        std::vector<uint8_t> remainingData(fileData.begin() + resumeOffset, fileData.end());
        std::string remainingChecksum = ProtocolUtils::calculateSHA256(remainingData);
        
        metadataMsg << "2:filename=" << metadata.filename
                   << "&size=" << remainingSize
                   << "&resume_offset=" << resumeOffset  
                   << "&checksum=" << remainingChecksum;
        
        LogPrint(eLogInfo, "ChunkedFileServer: Sending RESUME metadata: ", metadataMsg.str());
        
        size_t metadataSent = sendMessageOnStream(stream, metadataMsg.str());
        if (metadataSent == 0) {
            LogPrint(eLogError, "ChunkedFileServer: Failed to send resume metadata");
            return false;
        }
        
        // Step 2: Calculate starting chunk and offset within chunk
        size_t startChunkIndex = resumeOffset / metadata.chunkSize;
        size_t offsetInChunk = resumeOffset % metadata.chunkSize;
        
        LogPrint(eLogInfo, "ChunkedFileServer: Resume metadata sent, sending chunks from chunk ", 
                 startChunkIndex, " with ", offsetInChunk, " byte offset...");
        
        // Step 3: Send chunks from resume point
        int lastThreshold = 0;
        for (size_t chunkIndex = startChunkIndex; chunkIndex < metadata.chunkCount; ++chunkIndex) {
            if (!m_running.load()) {
                LogPrint(eLogInfo, "ChunkedFileServer: Resume transfer cancelled");
                return false;
            }
            
            // Calculate chunk boundaries
            size_t chunkStartOffset = chunkIndex * metadata.chunkSize;
            size_t chunkEndOffset = std::min((chunkIndex + 1) * metadata.chunkSize, fileData.size());
            
            // For first chunk, skip bytes already received
            if (chunkIndex == startChunkIndex && offsetInChunk > 0) {
                chunkStartOffset += offsetInChunk;
            }
            
            if (chunkStartOffset >= chunkEndOffset) {
                continue; // Skip if nothing to send in this chunk
            }
            
            size_t chunkSize = chunkEndOffset - chunkStartOffset;
            
            // Create chunk with header: [4 bytes chunk_index][4 bytes chunk_size][chunk_data]
            std::vector<uint8_t> chunkPacket;
            chunkPacket.reserve(8 + chunkSize);
            
            // Add chunk index (4 bytes, little-endian)
            uint32_t idx = static_cast<uint32_t>(chunkIndex);
            chunkPacket.push_back(idx & 0xFF);
            chunkPacket.push_back((idx >> 8) & 0xFF);
            chunkPacket.push_back((idx >> 16) & 0xFF);
            chunkPacket.push_back((idx >> 24) & 0xFF);
            
            // Add chunk size (4 bytes, little-endian)  
            uint32_t size = static_cast<uint32_t>(chunkSize);
            chunkPacket.push_back(size & 0xFF);
            chunkPacket.push_back((size >> 8) & 0xFF);
            chunkPacket.push_back((size >> 16) & 0xFF);
            chunkPacket.push_back((size >> 24) & 0xFF);
            
            // Add chunk data
            chunkPacket.insert(chunkPacket.end(), 
                              fileData.begin() + chunkStartOffset,
                              fileData.begin() + chunkEndOffset);
            
            size_t chunkSent = sendChunkOnStream(stream, chunkPacket);
            if (chunkSent != chunkPacket.size()) {
                LogPrint(eLogError, "ChunkedFileServer: Failed to send complete resume chunk ", chunkIndex, 
                         " - sent ", chunkSent, "/", chunkPacket.size(), " bytes");
                return false;
            }
            
            // Progress reporting (based on remaining chunks)
            double progress = 100.0 * (chunkIndex - startChunkIndex + 1) / (metadata.chunkCount - startChunkIndex);
            if (progress >= lastThreshold + 10) {
                lastThreshold += 10;
                LogPrint(eLogInfo, "ChunkedFileServer: Sent resume chunk ", chunkIndex + 1, "/",
                        metadata.chunkCount, " (", chunkSize, " data bytes + 8 header bytes)");
                std::cout << "Resume Progress: " << lastThreshold << "%\n";
            }
        }
        
        LogPrint(eLogInfo, "ChunkedFileServer: All resume chunks sent successfully from offset ", resumeOffset);
        return true;
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: Exception in sendFileOnStreamFromOffset: ", e.what());
        return false;
    }
}

size_t ChunkedFileServer::sendMessageOnStream(std::shared_ptr<stream::Stream> stream, const std::string& message)
{
    if (!stream) return 0;
    
    try {
        size_t sent = stream->Send(reinterpret_cast<const uint8_t*>(message.data()), message.size());
        LogPrint(eLogDebug, "ChunkedFileServer: Sent message on stream: ", sent, " bytes");
        return sent;
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: Exception sending message on stream: ", e.what());
        return 0;
    }
}

size_t ChunkedFileServer::sendChunkOnStream(std::shared_ptr<stream::Stream> stream, const std::vector<uint8_t>& chunkData)
{
    if (!stream || chunkData.empty()) return 0;
    
    try {
        size_t sent = stream->Send(chunkData.data(), chunkData.size());
        LogPrint(eLogDebug, "ChunkedFileServer: Sent chunk on stream: ", sent, " bytes");
        
        // Universal flow control - always present, works on all networks
        enforceUniversalFlowControl();
        
        return sent;
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: Exception sending chunk on stream: ", e.what());
        return 0;
    }
}

void ChunkedFileServer::enforceUniversalFlowControl()
{
    // Use the universal flow control system that adapts to network conditions
    UniversalFlowControl::EnforceFlowControl();
}

bool ChunkedFileServer::tryLoadFileFromDisk(const std::string& filename) 
{
    try {
        LogPrint(eLogInfo, "ChunkedFileServer: Attempting to load file from disk: ", filename);
        
        // Construct full path to input directory
        std::string fullPath = "../../data/input/" + filename;
        LogPrint(eLogInfo, "ChunkedFileServer: Trying to load file at path: ", fullPath);
        
        // Try to open the file
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            LogPrint(eLogError, "ChunkedFileServer: Could not open file: ", fullPath);
            return false;
        }
        
        // Get file size
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        if (size <= 0 || size > 1024 * 1024 * 1024) { // Limit to 1GB
            LogPrint(eLogWarning, "ChunkedFileServer: File size invalid or too large: ", size, " bytes");
            return false;
        }
        
        // Read file data
        std::vector<uint8_t> data(size);
        if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
            LogPrint(eLogWarning, "ChunkedFileServer: Failed to read file data: ", filename);
            return false;
        }
        
        // Add to server registry (extract just the filename for registry)
        std::string registryName = filename;
        size_t lastSlash = filename.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            registryName = filename.substr(lastSlash + 1);
        }
        
        addMockFile(registryName, data);
        
        LogPrint(eLogInfo, "ChunkedFileServer: Successfully loaded file: ", fullPath, 
                 " as '", registryName, "' (", size, " bytes)");
        return true;
        
    } catch (const std::exception& e) {
        LogPrint(eLogError, "ChunkedFileServer: Exception loading file ", filename, ": ", e.what());
        return false;
    }
}

} // namespace i2p::filetransfer