#pragma once

#include <chrono>
#include <atomic>
#include <memory>
#include <functional>
#include <mutex>
#include <unordered_map>

// Hash function for std::pair<uint32_t, uint32_t>
struct PairHash {
    std::size_t operator()(const std::pair<uint32_t, uint32_t>& p) const {
        std::size_t h1 = std::hash<uint32_t>{}(p.first);
        std::size_t h2 = std::hash<uint32_t>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

namespace i2p::core {

/**
 * @brief Stream stability metrics and failure detection
 */
struct StreamHealth {
    std::chrono::steady_clock::time_point lastActivity;
    int resendCount = 0;
    int tunnelSwitches = 0;
    bool isEstablished = false;
    bool isStable = true;
    double throughputKBps = 0.0;
    
    bool isHealthy() const {
        return isEstablished && isStable && resendCount < 3;
    }
    
    bool requiresRecovery() const {
        return resendCount >= 5 || tunnelSwitches >= 3 || !isStable;
    }
};

/**
 * @brief Monitors stream and tunnel stability for file transfers
 */
class StreamStabilityMonitor {
public:
    using StreamID = std::pair<uint32_t, uint32_t>; // (RecvStreamID, SendStreamID)
    using FailureCallback = std::function<void(StreamID, const StreamHealth&)>;
    
    static StreamStabilityMonitor& getInstance();
    
    // Stream lifecycle monitoring
    void registerStream(StreamID streamId);
    void updateStreamActivity(StreamID streamId);
    void recordResend(StreamID streamId);
    void recordTunnelSwitch(StreamID streamId);
    void recordThroughput(StreamID streamId, double kbps);
    void markStreamUnstable(StreamID streamId);
    void unregisterStream(StreamID streamId);
    
    // Health checking
    StreamHealth getStreamHealth(StreamID streamId) const;
    bool isStreamHealthy(StreamID streamId) const;
    bool streamRequiresRecovery(StreamID streamId) const;
    
    // Failure detection
    void setFailureCallback(FailureCallback callback);
    void checkAllStreams();
    
    // Configuration
    void setResendThreshold(int threshold) { m_resendThreshold = threshold; }
    void setTunnelSwitchThreshold(int threshold) { m_tunnelSwitchThreshold = threshold; }
    void setActivityTimeout(int seconds) { m_activityTimeoutSec = seconds; }
    
    // Cleanup (call before application shutdown)
    void shutdown();
    
private:
    StreamStabilityMonitor() = default;
    
    mutable std::mutex m_mutex;
    std::unordered_map<StreamID, StreamHealth, PairHash> m_streams;
    FailureCallback m_failureCallback;
    
    // Thresholds
    int m_resendThreshold = 3;
    int m_tunnelSwitchThreshold = 2;
    int m_activityTimeoutSec = 30;
    
    void checkStreamHealth(StreamID streamId, StreamHealth& health);
};

/**
 * @brief RAII stream registration helper
 */
class StreamMonitorGuard {
public:
    explicit StreamMonitorGuard(StreamStabilityMonitor::StreamID streamId);
    ~StreamMonitorGuard();
    
    void recordActivity();
    void recordResend();
    void recordTunnelSwitch();
    void recordThroughput(double kbps);
    void markUnstable();
    
    bool isHealthy() const;
    bool requiresRecovery() const;
    
private:
    StreamStabilityMonitor::StreamID m_streamId;
    bool m_registered;
};

} // namespace i2p::core