#pragma once

#include "Config.h"
#include <cstddef>
#include <chrono>
#include <string>

namespace i2p {
namespace filetransfer {

/**
 * @brief Configuration wrapper for file transfer parameters using hard-coded defaults
 * Note: i2pd config system has different API than expected, so using constants for now
 */
class TransferConfig {
public:
    // Network and connection timeouts
    static int getConnectionTimeout() {
        return 30000; // 30s default
    }
    
    static int getLeaseSetTimeout() {
        return 20000; // 20s default
    }
    
    static int getTransferTimeout() {
        return 30000; // 30s default
    }
    
    static int getStreamEstablishTimeout() {
        return 10000; // 10s default
    }
    
    // Retry parameters  
    static int getMaxRetries() {
        return 50; // 50 retries default
    }
    
    static int getRetryDelayMs() {
        return 100; // 100ms default
    }
    
    // File transfer parameters
    static size_t getDefaultChunkSize() {
        return 512; // 512 bytes default
    }
    
    static size_t getMaxFileSize() {
        return 100 * 1024 * 1024; // 100MB default
    }
    
    static size_t getMaxFilenameLength() {
        return 256; // 256 chars default
    }
    
    static size_t getMaxMessageSize() {
        return 4096; // 4KB default
    }
    
    // Buffer sizes
    static size_t getReceiveBufferSize() {
        return 65536; // 64KB default
    }
    
    static size_t getSendBufferSize() {
        return 8192; // 8KB default
    }
    
    // Performance settings
    static bool getEnablePerformanceLogging() {
        return false;
    }
    
    static bool getEnableDebugContext() {
        return false;  
    }
    
    // Application-specific defaults
    static size_t getDefaultMockFileSize() {
        return 100 * 1024 * 1024; // 100MB default
    }
    
    static std::string getDefaultMockFileName() {
        return "test.bin";
    }
    
    // Helper methods for common timeout patterns
    static std::chrono::milliseconds getConnectionTimeoutMs() {
        return std::chrono::milliseconds(getConnectionTimeout());
    }
    
    static std::chrono::milliseconds getRetryDelayMilliseconds() {
        return std::chrono::milliseconds(getRetryDelayMs());
    }
    
    static std::chrono::milliseconds getTransferTimeoutMs() {
        return std::chrono::milliseconds(getTransferTimeout());
    }
    
    // Chunk-specific timeouts
    static int getChunkTimeout() {
        return 15000; // 15 seconds per chunk
    }
    
    /**
     * @brief Initialize default configuration values
     * This can be extended in the future to load from actual i2pd config
     */
    static void initializeDefaults() {
        // For now this is a no-op, but ready for future config integration
    }
};

} // namespace filetransfer
} // namespace i2p