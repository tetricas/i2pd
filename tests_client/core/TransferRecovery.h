#pragma once

#include "StreamStabilityMonitor.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <atomic>

namespace i2p::core {

/**
 * @brief Transfer checkpoint for resuming interrupted transfers
 */
struct TransferCheckpoint {
    std::string serverB32;
    std::string filename;
    size_t totalSize = 0;
    size_t bytesReceived = 0;
    std::vector<uint8_t> partialData;
    std::string storagePath;  // Path to temp file for large transfers
    std::chrono::steady_clock::time_point lastUpdate;
    std::vector<uint8_t> checksum;
    int attemptCount = 0;
    
    bool isValid() const {
        return !serverB32.empty() && !filename.empty() && totalSize > 0;
    }
    
    double getProgress() const {
        return totalSize > 0 ? (double)bytesReceived / totalSize : 0.0;
    }
};

/**
 * @brief Recovery strategy for failed transfers
 */
enum class RecoveryStrategy {
    IMMEDIATE_RETRY,        // Retry immediately with same parameters
    DELAYED_RETRY,          // Wait before retrying
    CHECKPOINT_RESUME,      // Resume from last checkpoint
    PROTOCOL_SWITCH,        // Switch to different protocol
    CHUNKED_RECOVERY        // Break into smaller chunks
};

/**
 * @brief Transfer recovery coordinator
 */
class TransferRecovery {
public:
    using RecoveryCallback = std::function<bool(const TransferCheckpoint&, RecoveryStrategy)>;
    
    static TransferRecovery& getInstance();
    
    // Checkpoint management
    void saveCheckpoint(const std::string& transferId, const TransferCheckpoint& checkpoint);
    bool loadCheckpoint(const std::string& transferId, TransferCheckpoint& checkpoint);
    void clearCheckpoint(const std::string& transferId);
    
    // Recovery coordination
    void registerTransfer(const std::string& transferId, RecoveryCallback callback);
    void unregisterTransfer(const std::string& transferId);
    
    // Failure handling
    void handleStreamFailure(StreamStabilityMonitor::StreamID streamId, const StreamHealth& health);
    bool attemptRecovery(const std::string& transferId, RecoveryStrategy strategy);
    
    // Strategy selection
    RecoveryStrategy selectRecoveryStrategy(const TransferCheckpoint& checkpoint, const StreamHealth& health);
    
    // Configuration
    void setMaxRetryAttempts(int attempts) { m_maxRetryAttempts = attempts; }
    void setRetryDelay(int seconds) { m_retryDelaySec = seconds; }
    
    // Stream association (for TransferRecoveryGuard)
    void associateStreamWithTransfer(StreamStabilityMonitor::StreamID streamId, const std::string& transferId);
    
    // Cleanup (call before application shutdown)
    void shutdown();
    
private:
    TransferRecovery();
    
    std::mutex m_mutex;
    std::unordered_map<std::string, TransferCheckpoint> m_checkpoints;
    std::unordered_map<std::string, RecoveryCallback> m_callbacks;
    std::unordered_map<StreamStabilityMonitor::StreamID, std::string, PairHash> m_streamToTransfer;
    
    int m_maxRetryAttempts = 3;
    int m_retryDelaySec = 5;
    
    bool shouldRetry(const TransferCheckpoint& checkpoint) const;
    void scheduleDelayedRecovery(const std::string& transferId, int delaySec);
};

/**
 * @brief RAII transfer recovery registration
 */
class TransferRecoveryGuard {
public:
    TransferRecoveryGuard(const std::string& transferId, 
                         TransferRecovery::RecoveryCallback callback);
    ~TransferRecoveryGuard();
    
    void saveCheckpoint(const TransferCheckpoint& checkpoint);
    bool loadCheckpoint(TransferCheckpoint& checkpoint);
    void associateStream(StreamStabilityMonitor::StreamID streamId);
    
private:
    std::string m_transferId;
    bool m_registered;
};

} // namespace i2p::core