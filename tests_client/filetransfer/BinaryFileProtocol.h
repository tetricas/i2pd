#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <cstring>

namespace i2p::filetransfer
{

/**
 * @brief Binary file metadata structure (96 bytes, cache-friendly)
 */
struct BinaryFileMetadata
{
    uint64_t totalSize;         // 8 bytes - file size
    uint32_t chunkSize;         // 4 bytes - chunk size  
    uint32_t chunkCount;        // 4 bytes - total chunks
    uint8_t checksumType;       // 1 byte - hash algorithm (1 = SHA-256)
    uint8_t checksumLength;     // 1 byte - hash length (32)
    uint8_t checksum[32];       // 32 bytes - SHA-256 hash
    uint8_t filenameLength;     // 1 byte - filename length
    char filename[32];          // 32 bytes - filename (null-terminated)
    uint8_t reserved[13];       // 13 bytes - future use
    // Total: 96 bytes
} __attribute__((packed));

/**
 * @brief Binary chunk header structure (8 bytes)
 */
struct BinaryChunkHeader
{
    uint32_t chunkIndex;        // 4 bytes - chunk index
    uint32_t chunkSize;         // 4 bytes - actual chunk data size
} __attribute__((packed));

/**
 * @brief Binary file request structure
 */
struct BinaryFileRequest
{
    uint8_t messageType = 0x01; // FILE_REQUEST
    uint8_t filenameLength;     // 1 byte - filename length
    // filename follows immediately
} __attribute__((packed));

/**
 * @brief Utilities for binary file transfer protocol
 */
class BinaryProtocolUtils
{
public:
    /**
     * @brief Create binary file metadata
     */
    static BinaryFileMetadata createMetadata(const std::string& filename,
                                            const std::vector<uint8_t>& fileData,
                                            uint32_t chunkSize = 512 * 1024); // 512KB default
    
    /**
     * @brief Serialize metadata to bytes
     */
    static std::vector<uint8_t> serializeMetadata(const BinaryFileMetadata& metadata);
    
    /**
     * @brief Parse metadata from bytes
     */
    static bool parseMetadata(const uint8_t* data, size_t dataSize, BinaryFileMetadata& metadata);
    
    /**
     * @brief Create file request packet
     */
    static std::vector<uint8_t> createFileRequest(const std::string& filename);
    
    /**
     * @brief Parse filename from file request
     */
    static std::string parseFileRequest(const uint8_t* data, size_t dataSize);
    
    /**
     * @brief Create chunk header
     */
    static std::array<uint8_t, 8> createChunkHeader(uint32_t chunkIndex, uint32_t chunkSize);
    
    /**
     * @brief Parse chunk header
     */
    static bool parseChunkHeader(const uint8_t* data, BinaryChunkHeader& header);
    
    /**
     * @brief Convert hex string to binary
     */
    static std::vector<uint8_t> hexToBinary(const std::string& hex);
    
    /**
     * @brief Convert binary to hex string
     */
    static std::string binaryToHex(const uint8_t* data, size_t size);
    
    /**
     * @brief Calculate SHA-256 and return as binary
     */
    static std::array<uint8_t, 32> calculateSHA256Binary(const std::vector<uint8_t>& data);
    
    /**
     * @brief Verify data against binary checksum
     */
    static bool verifyChecksum(const std::vector<uint8_t>& data, const uint8_t* expectedChecksum);

private:
    // Little-endian conversion helpers
    template<typename T>
    static void writeLE(uint8_t*& ptr, T value) {
        std::memcpy(ptr, &value, sizeof(T));
        ptr += sizeof(T);
    }
    
    template<typename T>
    static T readLE(const uint8_t*& ptr) {
        T value;
        std::memcpy(&value, ptr, sizeof(T));
        ptr += sizeof(T);
        return value;
    }
};

} // namespace i2p::filetransfer