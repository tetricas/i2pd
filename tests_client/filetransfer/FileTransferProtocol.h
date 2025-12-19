#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <functional>
#include <cstring>
#include <fstream>
#include <memory>

namespace i2p::filetransfer
{

// Forward declaration
class ProtocolUtils;

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

// Virtual file interface for on-demand generation
class VirtualFile
{
public:
    virtual ~VirtualFile() = default;
    virtual size_t getSize() const = 0;
    virtual std::string getSeed() const = 0;
    virtual std::vector<uint8_t> generateChunk(size_t offset, size_t size) const = 0;
    virtual std::string calculateChecksum() const = 0;
    virtual std::string calculatePartialChecksum(size_t bytes) const = 0;
};

// Mock virtual file implementation for large files
class MockVirtualFile : public VirtualFile
{
private:
    size_t m_size;
    std::string m_seed;
    uint32_t m_seedHash;
    
public:
    MockVirtualFile(size_t size, const std::string& seed = "test") 
        : m_size(size), m_seed(seed)
    {
        constexpr std::hash<std::string> hasher;
        m_seedHash = static_cast<uint32_t>(hasher(seed));
    }
    
    size_t getSize() const override { return m_size; }
    std::string getSeed() const override { return m_seed; }
    
    std::vector<uint8_t> generateChunk(size_t offset, size_t size) const override
    {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            uint32_t value = m_seedHash ^ static_cast<uint32_t>(offset + i);
            value = value * 1664525 + 1013904223; // Linear congruential generator
            data[i] = static_cast<uint8_t>(value & 0xFF);
        }
        return data;
    }
    
    std::string calculateChecksum() const override
    {
        // For large files, calculate checksum progressively to avoid memory issues
        const size_t BUFFER_SIZE = 1024 * 1024; // 1MB buffer
        uint64_t hash = 14695981039346656037ULL; // FNV-1a 64-bit offset basis
        const uint64_t prime = 1099511628211ULL;  // FNV-1a 64-bit prime
        
        for (size_t offset = 0; offset < m_size; offset += BUFFER_SIZE) {
            size_t chunkSize = std::min(BUFFER_SIZE, m_size - offset);
            auto chunk = generateChunk(offset, chunkSize);
            
            for (uint8_t byte : chunk) {
                hash ^= byte;
                hash *= prime;
            }
        }
        
        std::stringstream ss;
        ss << std::hex << hash;
        return ss.str();
    }
    
    std::string calculatePartialChecksum(size_t bytes) const override
    {
        if (bytes > m_size) bytes = m_size;
        
        const size_t BUFFER_SIZE = 1024 * 1024; // 1MB buffer
        uint64_t hash = 14695981039346656037ULL; // FNV-1a 64-bit offset basis
        const uint64_t prime = 1099511628211ULL;  // FNV-1a 64-bit prime
        
        for (size_t offset = 0; offset < bytes; offset += BUFFER_SIZE) {
            size_t chunkSize = std::min(BUFFER_SIZE, bytes - offset);
            auto chunk = generateChunk(offset, chunkSize);
            
            for (uint8_t byte : chunk) {
                hash ^= byte;
                hash *= prime;
            }
        }
        
        std::stringstream ss;
        ss << std::hex << hash;
        return ss.str();
    }
};


// File metadata structure
struct FileMetadata
{
    std::string filename;
    size_t totalSize;
    size_t chunkSize;
    size_t chunkCount;
    std::string sha256Checksum;
    bool isVirtual = false;  // True if file is generated on-demand
    
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
    
    // Mock file generation has been moved to test/MockFileUtils.h
    // Use i2p::filetransfer::test::MockFileUtils for all test data generation
    
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

// Unified storage interface for handling both memory and file-based storage
class TransferStorage
{
public:
    virtual ~TransferStorage() = default;
    virtual void write(const std::vector<uint8_t>& data) = 0;
    virtual void append(const std::vector<uint8_t>& data) = 0;
    virtual std::vector<uint8_t> getData() const = 0;
    virtual size_t size() const = 0;
    virtual std::string calculateChecksum() const = 0;
    virtual std::string getStoragePath() const = 0;
    virtual void clear() = 0;
    virtual bool isFileBasedStorage() const = 0;
    virtual bool verify(const std::string& expectedChecksum) const = 0;
};

// Memory-based storage for small files
class MemoryStorage : public TransferStorage
{
private:
    std::vector<uint8_t> m_data;
    
public:
    void write(const std::vector<uint8_t>& data) override {
        m_data = data;
    }
    
    void append(const std::vector<uint8_t>& data) override {
        m_data.insert(m_data.end(), data.begin(), data.end());
    }
    
    std::vector<uint8_t> getData() const override {
        return m_data;
    }
    
    size_t size() const override {
        return m_data.size();
    }
    
    std::string calculateChecksum() const override {
        return ProtocolUtils::calculateSHA256(m_data);
    }
    
    std::string getStoragePath() const override {
        return "memory";
    }
    
    void clear() override {
        m_data.clear();
    }
    
    bool isFileBasedStorage() const override {
        return false;
    }
    
    bool verify(const std::string& expectedChecksum) const override {
        return calculateChecksum() == expectedChecksum;
    }
};

// File-based storage for large files
class FileStorage : public TransferStorage
{
private:
    std::string m_filePath;
    size_t m_currentSize = 0;
    
public:
    explicit FileStorage(const std::string& filePath) : m_filePath(filePath) {
        // Create directory if needed
        auto lastSlash = filePath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            std::string dir = filePath.substr(0, lastSlash);
            // Note: mkdir implementation would be platform-specific
            // For now, assume directory exists or will be created by caller
        }
    }
    
    ~FileStorage() {
        // Optionally clean up temp file
        // std::remove(m_filePath.c_str());
    }
    
    void write(const std::vector<uint8_t>& data) override {
        std::ofstream file(m_filePath, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("Failed to open file for writing: " + m_filePath);
        }
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        m_currentSize = data.size();
    }
    
    void append(const std::vector<uint8_t>& data) override {
        std::ofstream file(m_filePath, std::ios::binary | std::ios::app);
        if (!file) {
            throw std::runtime_error("Failed to open file for appending: " + m_filePath);
        }
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        m_currentSize += data.size();
    }
    
    std::vector<uint8_t> getData() const override {
        std::ifstream file(m_filePath, std::ios::binary);
        if (!file) {
            return {};
        }
        
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> data(fileSize);
        file.read(reinterpret_cast<char*>(data.data()), fileSize);
        return data;
    }
    
    size_t size() const override {
        return m_currentSize;
    }
    
    std::string calculateChecksum() const override {
        // Calculate checksum progressively to avoid loading entire file
        std::ifstream file(m_filePath, std::ios::binary);
        if (!file) {
            return "";
        }
        
        const size_t BUFFER_SIZE = 1024 * 1024; // 1MB buffer
        uint64_t hash = 14695981039346656037ULL; // FNV-1a 64-bit offset basis
        const uint64_t prime = 1099511628211ULL;  // FNV-1a 64-bit prime
        
        std::vector<uint8_t> buffer(BUFFER_SIZE);
        while (file) {
            file.read(reinterpret_cast<char*>(buffer.data()), BUFFER_SIZE);
            size_t bytesRead = file.gcount();
            
            for (size_t i = 0; i < bytesRead; ++i) {
                hash ^= buffer[i];
                hash *= prime;
            }
        }
        
        std::stringstream ss;
        ss << std::hex << hash;
        return ss.str();
    }
    
    std::string getStoragePath() const override {
        return m_filePath;
    }
    
    void clear() override {
        std::ofstream file(m_filePath, std::ios::binary | std::ios::trunc);
        m_currentSize = 0;
    }
    
    bool isFileBasedStorage() const override {
        return true;
    }
    
    bool verify(const std::string& expectedChecksum) const override {
        return calculateChecksum() == expectedChecksum;
    }
};

} // namespace i2p::filetransfer