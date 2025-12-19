#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstring>

namespace i2p {
namespace stream {
namespace performance {

/**
 * @brief Advanced file type detection for transfer optimization
 * 
 * This utility provides sophisticated file type detection to enable
 * appropriate performance optimizations:
 * - Binary format recognition for compression decisions
 * - Transfer profile selection based on file characteristics
 * - Optimization recommendations per file type
 */
class FileTypeDetection {
public:
    enum class TransferProfile {
        TEXT_OPTIMIZED,        // High compression, frequent ACKs for reliability
        BINARY_OPTIMIZED,      // Minimal compression, bulk transfer mode
        STREAMING_OPTIMIZED,   // Low latency, small buffers  
        ARCHIVE_OPTIMIZED,     // No compression, large MTU, minimal signing
        MEDIA_OPTIMIZED        // No compression, error recovery focus
    };

    struct FileAnalysis {
        std::string detectedFormat;
        TransferProfile recommendedProfile;
        bool shouldCompress;
        bool isBinaryFormat;
        double estimatedEntropy;
        size_t recommendedChunkSize;
        std::string analysisReason;
    };

private:
    // Extended file signature database for precise detection
    struct FileSignature {
        std::vector<uint8_t> signature;
        std::string format;
        TransferProfile profile;
        bool compressible;
        size_t optimalChunkSize;
    };

    static const std::vector<FileSignature>& getFileSignatures() {
        static const std::vector<FileSignature> signatures = {
        // Images (low compression benefit)
        {{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}, "PNG", TransferProfile::MEDIA_OPTIMIZED, false, 64*1024},
        {{0xFF, 0xD8, 0xFF}, "JPEG", TransferProfile::MEDIA_OPTIMIZED, false, 64*1024},
        {{0x47, 0x49, 0x46, 0x38}, "GIF", TransferProfile::MEDIA_OPTIMIZED, false, 32*1024},
        {{0x42, 0x4D}, "BMP", TransferProfile::MEDIA_OPTIMIZED, false, 128*1024},
        {{0x00, 0x00, 0x01, 0x00}, "ICO", TransferProfile::BINARY_OPTIMIZED, false, 16*1024},

        // Archives (already compressed)
        {{0x50, 0x4B, 0x03, 0x04}, "ZIP", TransferProfile::ARCHIVE_OPTIMIZED, false, 256*1024},
        {{0x50, 0x4B, 0x05, 0x06}, "ZIP", TransferProfile::ARCHIVE_OPTIMIZED, false, 256*1024},
        {{0x50, 0x4B, 0x07, 0x08}, "ZIP", TransferProfile::ARCHIVE_OPTIMIZED, false, 256*1024},
        {{0x1F, 0x8B, 0x08}, "GZIP", TransferProfile::ARCHIVE_OPTIMIZED, false, 128*1024},
        {{0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C}, "7Z", TransferProfile::ARCHIVE_OPTIMIZED, false, 512*1024},
        {{0x52, 0x61, 0x72, 0x21, 0x1A, 0x07}, "RAR", TransferProfile::ARCHIVE_OPTIMIZED, false, 256*1024},

        // Videos (already compressed)
        {{0x00, 0x00, 0x00, 0x14, 0x66, 0x74, 0x79, 0x70}, "MP4", TransferProfile::MEDIA_OPTIMIZED, false, 1024*1024},
        {{0x00, 0x00, 0x00, 0x18, 0x66, 0x74, 0x79, 0x70}, "MP4", TransferProfile::MEDIA_OPTIMIZED, false, 1024*1024},
        {{0x52, 0x49, 0x46, 0x46}, "AVI", TransferProfile::MEDIA_OPTIMIZED, false, 512*1024},
        {{0x1A, 0x45, 0xDF, 0xA3}, "MKV", TransferProfile::MEDIA_OPTIMIZED, false, 1024*1024},

        // Audio (already compressed)
        {{0xFF, 0xFB}, "MP3", TransferProfile::MEDIA_OPTIMIZED, false, 64*1024},
        {{0xFF, 0xF3}, "MP3", TransferProfile::MEDIA_OPTIMIZED, false, 64*1024},
        {{0xFF, 0xF2}, "MP3", TransferProfile::MEDIA_OPTIMIZED, false, 64*1024},
        {{0x4F, 0x67, 0x67, 0x53}, "OGG", TransferProfile::MEDIA_OPTIMIZED, false, 64*1024},

        // Documents (good compression)
        {{0x25, 0x50, 0x44, 0x46}, "PDF", TransferProfile::BINARY_OPTIMIZED, true, 128*1024},
        {{0xD0, 0xCF, 0x11, 0xE0}, "MS_OFFICE", TransferProfile::BINARY_OPTIMIZED, true, 64*1024},
        {{0x50, 0x4B, 0x03, 0x04}, "DOCX", TransferProfile::TEXT_OPTIMIZED, true, 32*1024}, // Actually ZIP-based

        // Executables (low compression benefit)
        {{0x7F, 0x45, 0x4C, 0x46}, "ELF", TransferProfile::BINARY_OPTIMIZED, false, 256*1024},
        {{0x4D, 0x5A}, "PE", TransferProfile::BINARY_OPTIMIZED, false, 512*1024},
        {{0xCA, 0xFE, 0xBA, 0xBE}, "MACH_O", TransferProfile::BINARY_OPTIMIZED, false, 256*1024},
        {{0xFE, 0xED, 0xFA, 0xCE}, "MACH_O", TransferProfile::BINARY_OPTIMIZED, false, 256*1024},

        // Source code (excellent compression)
        // Note: These would be detected by content analysis rather than signature
        };
        return signatures;
    }

    // File extension mappings for additional detection
    static const std::unordered_map<std::string, FileAnalysis>& getExtensionMap() {
        static const std::unordered_map<std::string, FileAnalysis> extensionMap = {
        // Source code files (high compression benefit)
        {".cpp", {"C++", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 16*1024, "Source code"}},
        {".c", {"C", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 16*1024, "Source code"}}, 
        {".h", {"C Header", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 8*1024, "Header file"}},
        {".py", {"Python", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 16*1024, "Python script"}},
        {".java", {"Java", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 16*1024, "Java source"}},
        {".js", {"JavaScript", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 16*1024, "JavaScript"}},
        {".html", {"HTML", TransferProfile::TEXT_OPTIMIZED, true, false, 0.5, 16*1024, "HTML document"}},
        {".xml", {"XML", TransferProfile::TEXT_OPTIMIZED, true, false, 0.5, 16*1024, "XML document"}},
        {".json", {"JSON", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 8*1024, "JSON data"}},

        // Configuration files (high compression benefit)
        {".conf", {"Config", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 4*1024, "Configuration"}},
        {".cfg", {"Config", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 4*1024, "Configuration"}},
        {".ini", {"INI", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 4*1024, "INI file"}},
        {".yml", {"YAML", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 8*1024, "YAML document"}},
        {".yaml", {"YAML", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 8*1024, "YAML document"}},

        // Text files (excellent compression benefit)  
        {".txt", {"Text", TransferProfile::TEXT_OPTIMIZED, true, false, 0.5, 8*1024, "Plain text"}},
        {".log", {"Log", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 16*1024, "Log file"}},
        {".md", {"Markdown", TransferProfile::TEXT_OPTIMIZED, true, false, 0.5, 8*1024, "Markdown"}},

        // Database files (variable compression)
        {".db", {"Database", TransferProfile::BINARY_OPTIMIZED, true, true, 0.7, 128*1024, "Database file"}},
        {".sqlite", {"SQLite", TransferProfile::BINARY_OPTIMIZED, true, true, 0.7, 128*1024, "SQLite database"}},
        {".sql", {"SQL", TransferProfile::TEXT_OPTIMIZED, true, false, 0.6, 16*1024, "SQL script"}},

        // System files
        {".so", {"Shared Library", TransferProfile::BINARY_OPTIMIZED, false, true, 0.8, 256*1024, "Shared library"}},
        {".dll", {"Windows DLL", TransferProfile::BINARY_OPTIMIZED, false, true, 0.8, 256*1024, "Windows library"}},
        {".dylib", {"macOS Library", TransferProfile::BINARY_OPTIMIZED, false, true, 0.8, 256*1024, "macOS library"}},
        };
        return extensionMap;
    }

public:
    /**
     * @brief Analyze file based on content and optional filename
     */
    static FileAnalysis analyzeFile(const uint8_t* data, size_t size, const std::string& filename = "") {
        FileAnalysis analysis{};
        
        // Try signature-based detection first
        analysis = detectBySignature(data, size);
        
        // If no signature match, try extension-based detection
        if (analysis.detectedFormat.empty() && !filename.empty()) {
            analysis = detectByExtension(filename);
        }
        
        // If still unknown, perform content analysis
        if (analysis.detectedFormat.empty()) {
            analysis = analyzeContent(data, size);
        }
        
        // Set recommended chunk size if not already set
        if (analysis.recommendedChunkSize == 0) {
            analysis.recommendedChunkSize = getDefaultChunkSize(analysis.recommendedProfile);
        }
        
        return analysis;
    }

    /**
     * @brief Analyze file by filename only (when content not available)
     */
    static FileAnalysis analyzeByFilename(const std::string& filename) {
        return detectByExtension(filename);
    }

    /**
     * @brief Get transfer optimization recommendations for detected file type
     */
    static BulkTransferOptimization::BulkTransferConfig getOptimizedConfig(
        const FileAnalysis& analysis, size_t fileSize = 0) {
        
        auto config = BulkTransferOptimization::BulkTransferConfig{};
        
        switch (analysis.recommendedProfile) {
            case TransferProfile::TEXT_OPTIMIZED:
                // Favor compression and reliability
                config.enableAdaptiveCompression = true;
                config.compressionProfile = AdaptiveCompression::CompressionProfile::AGGRESSIVE;
                config.signatureInterval = 4;           // More frequent signatures for reliability
                config.batchedACKInterval = 2;          // More frequent ACKs 
                config.targetMTU = 2048;                // Smaller chunks for text
                break;

            case TransferProfile::BINARY_OPTIMIZED:
                // Balanced approach for general binary files  
                config.enableAdaptiveCompression = true;
                config.compressionProfile = AdaptiveCompression::CompressionProfile::ADAPTIVE;
                config.signatureInterval = 8;           // Standard signature interval
                config.batchedACKInterval = 4;          // Standard ACK batching
                config.targetMTU = 3500;                // Standard large MTU
                break;

            case TransferProfile::ARCHIVE_OPTIMIZED:
                // Maximize throughput, minimal processing
                config.enableAdaptiveCompression = false; // Already compressed
                config.signatureInterval = 16;          // Maximum signature reduction
                config.batchedACKInterval = 8;          // Maximum ACK batching  
                config.targetMTU = 4000;                // Largest possible MTU
                config.isLargeBinaryFile = true;
                break;

            case TransferProfile::MEDIA_OPTIMIZED:
                // Optimize for large files with error recovery
                config.enableAdaptiveCompression = false; // Media already compressed
                config.signatureInterval = 12;          // Moderate signature reduction
                config.batchedACKInterval = 6;          // Moderate ACK batching
                config.targetMTU = 3500;                // Large MTU
                config.isLargeBinaryFile = true;
                break;

            case TransferProfile::STREAMING_OPTIMIZED:  
                // Optimize for low latency
                config.enableAdaptiveCompression = true;
                config.compressionProfile = AdaptiveCompression::CompressionProfile::CONSERVATIVE;
                config.signatureInterval = 2;           // Very frequent signatures
                config.batchedACKInterval = 1;          // No ACK batching
                config.targetMTU = 1500;                // Smaller MTU for low latency
                break;
        }
        
        // Adjust for file size if provided
        if (fileSize > 100 * 1024 * 1024) { // >100MB
            config.isLargeBinaryFile = true;
            config.signatureInterval *= 2;     // Reduce signing frequency for large files
            config.batchedACKInterval *= 2;    // Reduce ACK frequency for large files
        }
        
        config.estimatedTotalSize = fileSize;
        return config;
    }

private:
    static FileAnalysis detectBySignature(const uint8_t* data, size_t size) {
        FileAnalysis analysis{};
        
        const auto& signatures = getFileSignatures();
        for (const auto& sig : signatures) {
            if (size >= sig.signature.size() && 
                std::memcmp(data, sig.signature.data(), sig.signature.size()) == 0) {
                
                analysis.detectedFormat = sig.format;
                analysis.recommendedProfile = sig.profile;
                analysis.shouldCompress = sig.compressible;
                analysis.isBinaryFormat = true;
                analysis.recommendedChunkSize = sig.optimalChunkSize;
                analysis.analysisReason = "Signature match: " + sig.format;
                break;
            }
        }
        
        return analysis;
    }

    static FileAnalysis detectByExtension(const std::string& filename) {
        FileAnalysis analysis{};
        
        // Extract extension
        size_t dotPos = filename.find_last_of('.');
        if (dotPos == std::string::npos) {
            return analysis; // No extension
        }
        
        std::string extension = filename.substr(dotPos);
        // Convert to lowercase for matching
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        
        const auto& extensionMap = getExtensionMap();
        auto it = extensionMap.find(extension);
        if (it != extensionMap.end()) {
            analysis = it->second;
            analysis.analysisReason = "Extension match: " + extension;
        }
        
        return analysis;
    }

    static FileAnalysis analyzeContent(const uint8_t* data, size_t size) {
        FileAnalysis analysis{};
        
        // Perform basic content analysis
        size_t sampleSize = std::min(size, static_cast<size_t>(1024));
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

        // Calculate rough entropy
        analysis.estimatedEntropy = AdaptiveCompression::calculateEntropy(data, size);
        
        // Make classification decisions
        if (nullCount > sampleSize / 10) {
            // High null ratio indicates binary
            analysis.detectedFormat = "Binary";
            analysis.recommendedProfile = TransferProfile::BINARY_OPTIMIZED;
            analysis.isBinaryFormat = true;
            analysis.shouldCompress = (analysis.estimatedEntropy < 0.8);
            analysis.analysisReason = "High null byte ratio";
        } else if (printableCount > (sampleSize * 4) / 5) {
            // High printable ratio indicates text
            analysis.detectedFormat = "Text"; 
            analysis.recommendedProfile = TransferProfile::TEXT_OPTIMIZED;
            analysis.isBinaryFormat = false;
            analysis.shouldCompress = true;
            analysis.analysisReason = "High printable character ratio";
        } else {
            // Mixed content, use entropy for decision
            analysis.detectedFormat = "Mixed";
            analysis.recommendedProfile = TransferProfile::BINARY_OPTIMIZED;
            analysis.isBinaryFormat = true;
            analysis.shouldCompress = (analysis.estimatedEntropy < 0.7);
            analysis.analysisReason = "Content analysis";
        }
        
        analysis.recommendedChunkSize = getDefaultChunkSize(analysis.recommendedProfile);
        return analysis;
    }

    static size_t getDefaultChunkSize(TransferProfile profile) {
        switch (profile) {
            case TransferProfile::TEXT_OPTIMIZED: return 16 * 1024;    // 16KB
            case TransferProfile::BINARY_OPTIMIZED: return 64 * 1024;  // 64KB  
            case TransferProfile::STREAMING_OPTIMIZED: return 4 * 1024; // 4KB
            case TransferProfile::ARCHIVE_OPTIMIZED: return 512 * 1024; // 512KB
            case TransferProfile::MEDIA_OPTIMIZED: return 256 * 1024;   // 256KB
            default: return 32 * 1024; // 32KB default
        }
    }
};

} // namespace performance
} // namespace stream
} // namespace i2p