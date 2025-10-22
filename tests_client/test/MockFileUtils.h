#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace i2p::filetransfer::test {

/**
 * @brief Centralized mock file generation utilities for testing
 * 
 * This class consolidates all mock file generation functionality
 * that was previously scattered across production interfaces.
 */
class MockFileUtils {
public:
    /**
     * @brief Generate mock file data with specified size and seed
     * @param size Size in bytes
     * @param seed Random seed for reproducible content
     * @return Vector containing mock file data
     */
    static std::vector<uint8_t> generateMockFile(size_t size, const std::string& seed = "test") {
        if (size > 500 * 1024 * 1024) { // >500MB - use efficient generation
            return generateLargeMockFile(size, seed);
        }
        
        std::vector<uint8_t> data;
        data.reserve(size);
        
        // Use seed to create deterministic but pseudo-random content
        std::hash<std::string> hasher;
        uint32_t seedHash = static_cast<uint32_t>(hasher(seed));
        
        for (size_t i = 0; i < size; ++i) {
            // Simple PRNG based on seed and position
            uint32_t val = (seedHash * 1103515245 + 12345 + static_cast<uint32_t>(i)) % 256;
            data.push_back(static_cast<uint8_t>(val));
        }
        
        return data;
    }
    
    /**
     * @brief Generate large mock files efficiently (chunked generation)
     * @param size Total size in bytes
     * @param seed Random seed
     * @return Vector containing mock file data
     */
    static std::vector<uint8_t> generateLargeMockFile(size_t size, const std::string& seed = "test") {
        std::vector<uint8_t> data;
        data.reserve(size);
        
        const size_t chunkSize = 1024 * 1024; // 1MB chunks
        size_t remaining = size;
        size_t globalOffset = 0;
        
        while (remaining > 0) {
            size_t currentChunkSize = std::min(chunkSize, remaining);
            auto chunk = generateMockFileChunk(currentChunkSize, seed, globalOffset);
            data.insert(data.end(), chunk.begin(), chunk.end());
            
            remaining -= currentChunkSize;
            globalOffset += currentChunkSize;
        }
        
        return data;
    }
    
    /**
     * @brief Generate a chunk of mock file data
     * @param size Chunk size in bytes
     * @param seed Base seed
     * @param globalOffset Global offset in the file
     * @return Vector containing chunk data
     */
    static std::vector<uint8_t> generateMockFileChunk(size_t size, const std::string& seed, size_t globalOffset) {
        std::vector<uint8_t> chunk;
        chunk.reserve(size);
        
        // Combine seed with global offset for chunk-specific seed
        std::string chunkSeed = seed + "_chunk_" + std::to_string(globalOffset / (1024 * 1024));
        std::hash<std::string> hasher;
        uint32_t seedHash = static_cast<uint32_t>(hasher(chunkSeed));
        
        for (size_t i = 0; i < size; ++i) {
            uint32_t val = (seedHash * 1103515245 + 12345 + static_cast<uint32_t>(globalOffset + i)) % 256;
            chunk.push_back(static_cast<uint8_t>(val));
        }
        
        return chunk;
    }
    
    /**
     * @brief Add mock file to server implementations (helper for tests)
     * @param filename Name of the file
     * @param size Size in bytes
     * @param seed Random seed
     * @return Generated file data
     */
    static std::vector<uint8_t> createTestFile(const std::string& filename, size_t size, const std::string& seed = "") {
        std::string actualSeed = seed.empty() ? ("test_" + filename) : seed;
        return generateMockFile(size, actualSeed);
    }
};

} // namespace i2p::filetransfer::test