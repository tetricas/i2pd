#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <algorithm>
#include <cmath>

namespace i2p {
namespace stream {
namespace performance {

/**
 * @brief Adaptive compression system for optimizing file transfer performance
 * 
 * This system implements security-validated optimizations identified in the 
 * performance vs security analysis. It provides:
 * - Binary file format detection
 * - Entropy-based compression decisions  
 * - File type-specific optimization profiles
 * - Zero security impact - pure performance optimization
 */
class AdaptiveCompression {
public:
    enum class FileType {
        TEXT,           // High compression benefit (logs, configs, source code)
        BINARY,         // Low compression benefit (executables, databases)
        COMPRESSED,     // Already compressed (images, videos, archives)
        UNKNOWN         // Default conservative behavior
    };

    enum class CompressionProfile {
        AGGRESSIVE,     // Always compress, good for text files
        ADAPTIVE,       // Entropy-based decision making
        CONSERVATIVE,   // Minimal compression, preserve CPU
        DISABLED        // No compression, maximum throughput
    };

    struct CompressionDecision {
        bool shouldCompress;
        FileType detectedType;
        double entropy;
        size_t estimatedCpuCost;    // in microseconds
        size_t estimatedSavings;    // in bytes
    };

private:
    static const double ENTROPY_THRESHOLD;       // Below this, compression likely beneficial
    static const size_t BINARY_DETECTION_SAMPLE;  // First KB for format detection
    static const size_t ENTROPY_CALCULATION_SAMPLE; // Sample for entropy calculation

    // Common binary file signatures for fast detection
    static const std::array<std::pair<const char*, size_t>, 12>& getBinarySignatures() {
        static const std::array<std::pair<const char*, size_t>, 12> signatures = {{
            {"\x89PNG", 4},          // PNG
            {"\xFF\xD8\xFF", 3},     // JPEG  
            {"GIF8", 4},             // GIF
            {"PK\x03\x04", 4},       // ZIP/JAR/APK
            {"\x1F\x8B", 2},         // GZIP
            {"7z\xBC\xAF", 5},       // 7-Zip
            {"\x00\x00\x01\x00", 4}, // ICO
            {"BM", 2},               // BMP
            {"RIFF", 4},             // WAV/AVI  
            {"\x25PDF", 5},          // PDF
            {"\x7F""ELF", 4},        // ELF executable (split string to avoid hex escape issue)
            {"\x4D\x5A", 2}          // Windows PE executable
        }};
        return signatures;
    }

public:
    /**
     * @brief Analyze data and decide whether compression should be applied
     * @param data Pointer to data buffer  
     * @param len Length of data
     * @param profile Compression profile to use
     * @return Compression decision with analysis details
     */
    static CompressionDecision analyzeData(const uint8_t* data, size_t len, 
                                         CompressionProfile profile = CompressionProfile::ADAPTIVE) {
        CompressionDecision decision{};
        
        if (!data || len == 0) {
            decision.shouldCompress = false;
            decision.detectedType = FileType::UNKNOWN;
            return decision;
        }

        // Fast path for profile-based decisions
        if (profile == CompressionProfile::DISABLED) {
            decision.shouldCompress = false;
            decision.detectedType = FileType::UNKNOWN;
            return decision;
        }
        
        if (profile == CompressionProfile::AGGRESSIVE) {
            decision.shouldCompress = true;
            decision.detectedType = FileType::TEXT;
            decision.estimatedCpuCost = estimateCompressionCost(len);
            return decision;
        }

        // Detect file type from content
        decision.detectedType = detectFileType(data, len);
        
        // Conservative approach for known compressed formats
        if (decision.detectedType == FileType::COMPRESSED) {
            decision.shouldCompress = false;
            decision.entropy = 0.9; // High entropy assumption
            return decision;
        }

        // Calculate entropy for adaptive decision
        if (profile == CompressionProfile::ADAPTIVE) {
            decision.entropy = calculateEntropy(data, len);
            decision.shouldCompress = (decision.entropy < ENTROPY_THRESHOLD);
            decision.estimatedSavings = estimateCompressionSavings(len, decision.entropy);
            decision.estimatedCpuCost = estimateCompressionCost(len);
        } else { // CONSERVATIVE
            decision.entropy = calculateEntropy(data, len);
            decision.shouldCompress = (decision.entropy < ENTROPY_THRESHOLD * 0.5); // More conservative
            decision.estimatedCpuCost = estimateCompressionCost(len);
        }

        return decision;
    }

    /**
     * @brief Detect file type based on content analysis
     */
    static FileType detectFileType(const uint8_t* data, size_t len) {
        size_t sampleSize = std::min(len, BINARY_DETECTION_SAMPLE);
        
        // Check for binary signatures
        const auto& signatures = getBinarySignatures();
        for (const auto& signature : signatures) {
            if (len >= signature.second && 
                std::memcmp(data, signature.first, signature.second) == 0) {
                return FileType::COMPRESSED;
            }
        }

        // Check for high ratio of printable ASCII characters
        size_t printableCount = 0;
        size_t nullCount = 0;
        
        for (size_t i = 0; i < sampleSize; ++i) {
            uint8_t byte = data[i];
            if (byte == 0) {
                nullCount++;
            } else if ((byte >= 32 && byte <= 126) || byte == '\t' || 
                       byte == '\n' || byte == '\r') {
                printableCount++;
            }
        }

        // High null byte ratio indicates binary
        if (nullCount > sampleSize / 10) {
            return FileType::BINARY;
        }

        // High printable ratio indicates text
        if (printableCount > (sampleSize * 4) / 5) {
            return FileType::TEXT;
        }

        return FileType::BINARY; // Conservative default
    }

    /**
     * @brief Calculate Shannon entropy of data sample
     */
    static double calculateEntropy(const uint8_t* data, size_t len) {
        size_t sampleSize = std::min(len, ENTROPY_CALCULATION_SAMPLE);
        std::array<size_t, 256> frequency{};
        
        // Count byte frequencies
        for (size_t i = 0; i < sampleSize; ++i) {
            frequency[data[i]]++;
        }

        // Calculate Shannon entropy
        double entropy = 0.0;
        for (size_t count : frequency) {
            if (count > 0) {
                double probability = static_cast<double>(count) / sampleSize;
                entropy -= probability * std::log2(probability);
            }
        }

        return entropy / 8.0; // Normalize to 0-1 range
    }

    /**
     * @brief Estimate CPU cost of compression in microseconds
     */
    static size_t estimateCompressionCost(size_t dataSize) {
        // Rough estimate: ~0.5 microseconds per KB for gzip compression
        return (dataSize / 1024) * 1 + 10; // Base cost + per-KB cost
    }

    /**
     * @brief Estimate compression savings based on entropy  
     */
    static size_t estimateCompressionSavings(size_t dataSize, double entropy) {
        if (entropy > ENTROPY_THRESHOLD) {
            return dataSize / 10; // Minimal savings for high entropy
        }
        
        // Rough estimate based on typical compression ratios
        double compressionRatio = 1.0 - (entropy * 0.8); // 0.2 to 0.8 compression ratio
        return static_cast<size_t>(dataSize * compressionRatio);
    }

    /**
     * @brief Get recommended profile for file transfer scenarios
     */
    static CompressionProfile getRecommendedProfile(bool isBulkTransfer, bool isCpuLimited) {
        if (isCpuLimited) {
            return CompressionProfile::CONSERVATIVE;
        }
        
        if (isBulkTransfer) {
            return CompressionProfile::ADAPTIVE;
        }
        
        return CompressionProfile::AGGRESSIVE; // Interactive transfers benefit from compression
    }
};

// Static member definitions
inline const double AdaptiveCompression::ENTROPY_THRESHOLD = 0.7;
inline const size_t AdaptiveCompression::BINARY_DETECTION_SAMPLE = 1024;
inline const size_t AdaptiveCompression::ENTROPY_CALCULATION_SAMPLE = 512;

} // namespace performance  
} // namespace stream
} // namespace i2p