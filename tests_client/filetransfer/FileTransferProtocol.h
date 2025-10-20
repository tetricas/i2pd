#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <functional>
#include <cstring>

namespace i2p::filetransfer
{

// Protocol constants
constexpr size_t DEFAULT_CHUNK_SIZE = 512;
constexpr size_t MAX_FILENAME_LENGTH = 256;
constexpr size_t MESSAGE_HEADER_SIZE = 7; // TYPE(1) + LENGTH(2) + CHECKSUM(4)
constexpr size_t MAX_MESSAGE_SIZE = 4096;

// Message types
enum class MessageType : uint8_t
{
    FILE_REQUEST = 0x01,        // Client requests file
    METADATA_RESPONSE = 0x02,   // Server sends file metadata  
    CHUNK_REQUEST = 0x03,       // Client requests specific chunk
    CHUNK_DATA = 0x04,          // Server sends chunk data
    TRANSFER_COMPLETE = 0x05,   // Client confirms transfer
    RESUME_REQUEST = 0x06,      // Client requests resume from offset
    RESUME_RESPONSE = 0x07,     // Server confirms resume capability
    ERROR_RESPONSE = 0xFF       // Error handling
};

// Error codes
enum class ErrorCode : uint8_t
{
    FILE_NOT_FOUND = 0x01,
    CHUNK_OUT_OF_RANGE = 0x02,
    CHECKSUM_MISMATCH = 0x03,
    INVALID_REQUEST = 0x04,
    RESUME_NOT_SUPPORTED = 0x05,
    RESUME_INVALID_OFFSET = 0x06,
    SERVER_ERROR = 0xFF
};

// Resume request data
struct ResumeRequest
{
    std::string filename;
    size_t resumeOffset;          // Byte offset to resume from
    std::string partialChecksum;  // Checksum of data already received
    
    ResumeRequest() = default;
    ResumeRequest(const std::string& file, size_t offset, const std::string& checksum)
        : filename(file), resumeOffset(offset), partialChecksum(checksum) {}
        
    // Calculate which chunk to start from
    size_t getStartChunkIndex(size_t chunkSize) const {
        return resumeOffset / chunkSize;
    }
    
    // Calculate offset within start chunk
    size_t getChunkOffset(size_t chunkSize) const {
        return resumeOffset % chunkSize;
    }
};

// Resume response data  
struct ResumeResponse
{
    bool canResume;
    size_t confirmedOffset;       // Server confirmed resume point
    std::string remainingChecksum; // Checksum of remaining data
    std::string errorMessage;
    
    ResumeResponse() : canResume(false), confirmedOffset(0) {}
    ResumeResponse(bool resume, size_t offset) 
        : canResume(resume), confirmedOffset(offset) {}
};

// File metadata structure
struct FileMetadata
{
    std::string filename;
    size_t totalSize;
    size_t chunkSize;
    size_t chunkCount;
    std::string sha256Checksum;
    
    FileMetadata() = default;
    FileMetadata(const std::string& name, size_t size, size_t chunk_size = DEFAULT_CHUNK_SIZE)
        : filename(name), totalSize(size), chunkSize(chunk_size)
    {
        chunkCount = (size + chunk_size - 1) / chunk_size; // Ceiling division
    }
};

// Transfer statistics
struct TransferStats
{
    std::chrono::steady_clock::time_point initTime{};
    std::chrono::steady_clock::time_point requestTime{};
    std::chrono::steady_clock::time_point startTime{};
    std::chrono::steady_clock::time_point endTime{};
    size_t totalBytes = 0;
    size_t chunksReceived = 0;
    size_t chunksTotal = 0;
    std::vector<std::chrono::milliseconds> chunkTimes;
    bool verified = false;
    
    std::chrono::milliseconds getInitTime() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(requestTime - initTime);
    }

    std::chrono::milliseconds getRequestTime() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(startTime - requestTime);
    }

    std::chrono::milliseconds getTransferTime() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    }
    
    std::chrono::milliseconds getAverageChunkTime() const {
        if (chunkTimes.empty()) return std::chrono::milliseconds(0);
        auto total = std::chrono::milliseconds(0);
        for (const auto& t : chunkTimes) total += t;
        return total / chunkTimes.size();
    }
    
    double getThroughputKBps() const {
        auto totalMs = getTransferTime().count();
        if (totalMs == 0) return 0.0;
        return (totalBytes / 1024.0) / (totalMs / 1000.0);
    }
};

// Utility functions
class ProtocolUtils
{
public:
    // Calculate simple hash checksum of data (CRC32-like)
    static std::string calculateSHA256(const std::vector<uint8_t>& data)
    {
        uint64_t hash = 14695981039346656037ULL; // FNV-1a 64-bit offset basis
        const uint64_t prime = 1099511628211ULL;  // FNV-1a 64-bit prime
        
        for (uint8_t byte : data) {
            hash ^= byte;
            hash *= prime;
        }
        
        // Convert to hex string
        std::stringstream ss;
        ss << std::hex << hash;
        return ss.str();
    }
    
    // Generate mock file data with predictable pattern (memory-efficient for large files)
    static std::vector<uint8_t> generateMockFile(size_t size, const std::string& seed = "test")
    {
        // For very large files (>50MB), refuse to generate in-memory and suggest file-based approach
        const size_t MAX_SAFE_SIZE = 50 * 1024 * 1024; // 50MB safety limit
        
        if (size > MAX_SAFE_SIZE) {
            throw std::runtime_error("File size too large for in-memory generation: " + 
                                   std::to_string(size) + " bytes. Use file-based approach instead.");
        }
        
        // Generate data in chunks to avoid large contiguous allocation
        std::vector<uint8_t> data;
        const size_t CHUNK_SIZE = 1024 * 1024; // 1MB chunks
        
        constexpr std::hash<std::string> hasher;
        const auto seedHash = static_cast<uint32_t>(hasher(seed));
        
        for (size_t offset = 0; offset < size; offset += CHUNK_SIZE) {
            size_t currentChunkSize = std::min(CHUNK_SIZE, size - offset);
            
            // Generate chunk
            std::vector<uint8_t> chunk(currentChunkSize);
            for (size_t i = 0; i < currentChunkSize; ++i) {
                uint32_t value = seedHash ^ static_cast<uint32_t>(offset + i);
                value = value * 1664525 + 1013904223; // Linear congruential generator
                chunk[i] = static_cast<uint8_t>(value & 0xFF);
            }
            
            // Append to result
            data.insert(data.end(), chunk.begin(), chunk.end());
        }
        
        return data;
    }
    
    // Generate a chunk of mock data (helper for large files)
    static std::vector<uint8_t> generateMockFileChunk(size_t size, const std::string& seed, size_t globalOffset)
    {
        std::vector<uint8_t> data(size);
        
        // Create predictable pattern based on seed and position
        constexpr std::hash<std::string> hasher;
        const auto seedHash = static_cast<uint32_t>(hasher(seed));
        
        for (size_t i = 0; i < size; ++i) {
            // Create pseudo-random but reproducible pattern
            uint32_t value = seedHash ^ static_cast<uint32_t>(globalOffset + i);
            value = value * 1664525 + 1013904223; // Linear congruential generator
            data[i] = static_cast<uint8_t>(value & 0xFF);
        }
        
        return data;
    }
    
    // Serialize message with header
    static std::string serializeMessage(MessageType type, const std::string& payload)
    {
        if (payload.size() > MAX_MESSAGE_SIZE - MESSAGE_HEADER_SIZE) {
            throw std::runtime_error("Message payload too large");
        }
        
        std::string message;
        message.resize(MESSAGE_HEADER_SIZE + payload.size());
        
        // Message format: [TYPE:1][LENGTH:2][PAYLOAD:N][CHECKSUM:4]
        message[0] = static_cast<uint8_t>(type);
        
        uint16_t payloadLen = static_cast<uint16_t>(payload.size());
        message[1] = static_cast<uint8_t>(payloadLen >> 8);
        message[2] = static_cast<uint8_t>(payloadLen & 0xFF);
        
        // Copy payload
        std::memcpy(&message[3], payload.data(), payload.size());
        
        // Calculate and append checksum (simple CRC32-like)
        uint32_t checksum = calculateChecksum(message.data(), 3 + payload.size());
        size_t checksumPos = 3 + payload.size();
        message[checksumPos] = static_cast<uint8_t>(checksum >> 24);
        message[checksumPos + 1] = static_cast<uint8_t>((checksum >> 16) & 0xFF);
        message[checksumPos + 2] = static_cast<uint8_t>((checksum >> 8) & 0xFF);
        message[checksumPos + 3] = static_cast<uint8_t>(checksum & 0xFF);
        
        return message;
    }
    
    // Parse message and validate checksum
    static bool parseMessage(const std::string& message, MessageType& type, std::string& payload)
    {
        if (message.size() < MESSAGE_HEADER_SIZE) return false;
        
        type = static_cast<MessageType>(static_cast<uint8_t>(message[0]));
        
        uint16_t payloadLen = (static_cast<uint8_t>(message[1]) << 8) | 
                              static_cast<uint8_t>(message[2]);
        
        if (message.size() != MESSAGE_HEADER_SIZE + payloadLen) return false;
        
        // Verify checksum
        uint32_t expectedChecksum = calculateChecksum(message.data(), 3 + payloadLen);
        size_t checksumPos = 3 + payloadLen;
        uint32_t actualChecksum = (static_cast<uint8_t>(message[checksumPos]) << 24) |
                                  (static_cast<uint8_t>(message[checksumPos + 1]) << 16) |
                                  (static_cast<uint8_t>(message[checksumPos + 2]) << 8) |
                                  static_cast<uint8_t>(message[checksumPos + 3]);
        
        if (expectedChecksum != actualChecksum) return false;
        
        payload = message.substr(3, payloadLen);
        return true;
    }
    
private:
    // Simple checksum calculation
    static uint32_t calculateChecksum(const void* data, size_t length)
    {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint32_t checksum = 0;
        for (size_t i = 0; i < length; ++i) {
            checksum = checksum * 31 + bytes[i];
        }
        return checksum;
    }
};

} // namespace i2p::filetransfer