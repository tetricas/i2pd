#include "StreamStabilityMonitor.h"
#include "Log.h"
#include <algorithm>

namespace i2p::core {

StreamStabilityMonitor& StreamStabilityMonitor::getInstance() {
    static StreamStabilityMonitor instance;
    return instance;
}

void StreamStabilityMonitor::registerStream(StreamID streamId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    StreamHealth health;
    health.lastActivity = std::chrono::steady_clock::now();
    health.isEstablished = true;
    health.isStable = true;
    
    m_streams[streamId] = health;
    
    LogPrint(eLogInfo, "StreamMonitor: Registered stream [", streamId.first, ",", streamId.second, "]");
}

void StreamStabilityMonitor::updateStreamActivity(StreamID streamId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_streams.find(streamId);
    if (it != m_streams.end()) {
        it->second.lastActivity = std::chrono::steady_clock::now();
    }
}

void StreamStabilityMonitor::recordResend(StreamID streamId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_streams.find(streamId);
    if (it != m_streams.end()) {
        it->second.resendCount++;
        
        LogPrint(eLogWarning, "StreamMonitor: Stream [", streamId.first, ",", streamId.second, 
                 "] resend #", it->second.resendCount);
        
        if (it->second.resendCount >= m_resendThreshold) {
            it->second.isStable = false;
            LogPrint(eLogError, "StreamMonitor: Stream [", streamId.first, ",", streamId.second, 
                     "] marked unstable after ", it->second.resendCount, " resends");
            
            if (m_failureCallback) {
                m_failureCallback(streamId, it->second);
            }
        }
    }
}

void StreamStabilityMonitor::recordTunnelSwitch(StreamID streamId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_streams.find(streamId);
    if (it != m_streams.end()) {
        it->second.tunnelSwitches++;
        
        LogPrint(eLogWarning, "StreamMonitor: Stream [", streamId.first, ",", streamId.second, 
                 "] tunnel switch #", it->second.tunnelSwitches);
        
        if (it->second.tunnelSwitches >= m_tunnelSwitchThreshold) {
            it->second.isStable = false;
            LogPrint(eLogError, "StreamMonitor: Stream [", streamId.first, ",", streamId.second, 
                     "] marked unstable after ", it->second.tunnelSwitches, " tunnel switches");
        }
    }
}

void StreamStabilityMonitor::recordThroughput(StreamID streamId, double kbps) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_streams.find(streamId);
    if (it != m_streams.end()) {
        it->second.throughputKBps = kbps;
        
        // Detect severe throughput degradation
        if (kbps < 100.0) { // Less than 100 KB/s indicates problems
            LogPrint(eLogWarning, "StreamMonitor: Stream [", streamId.first, ",", streamId.second, 
                     "] low throughput: ", kbps, " KB/s");
        }
    }
}

void StreamStabilityMonitor::markStreamUnstable(StreamID streamId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_streams.find(streamId);
    if (it != m_streams.end()) {
        it->second.isStable = false;
        LogPrint(eLogError, "StreamMonitor: Stream [", streamId.first, ",", streamId.second, 
                 "] manually marked unstable");
        
        if (m_failureCallback) {
            m_failureCallback(streamId, it->second);
        }
    }
}

void StreamStabilityMonitor::unregisterStream(StreamID streamId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_streams.find(streamId);
    if (it != m_streams.end()) {
        LogPrint(eLogInfo, "StreamMonitor: Unregistered stream [", streamId.first, ",", streamId.second, 
                 "] - resends:", it->second.resendCount, " tunnelSwitches:", it->second.tunnelSwitches);
        m_streams.erase(it);
    }
}

StreamHealth StreamStabilityMonitor::getStreamHealth(StreamID streamId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_streams.find(streamId);
    if (it != m_streams.end()) {
        return it->second;
    }
    
    return StreamHealth{}; // Default unhealthy state
}

bool StreamStabilityMonitor::isStreamHealthy(StreamID streamId) const {
    auto health = getStreamHealth(streamId);
    return health.isHealthy();
}

bool StreamStabilityMonitor::streamRequiresRecovery(StreamID streamId) const {
    auto health = getStreamHealth(streamId);
    return health.requiresRecovery();
}

void StreamStabilityMonitor::setFailureCallback(FailureCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_failureCallback = callback;
}

void StreamStabilityMonitor::checkAllStreams() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto now = std::chrono::steady_clock::now();
    
    for (auto& [streamId, health] : m_streams) {
        checkStreamHealth(streamId, health);
        
        // Check for activity timeout
        auto timeSinceActivity = std::chrono::duration_cast<std::chrono::seconds>(
            now - health.lastActivity).count();
        
        if (timeSinceActivity > m_activityTimeoutSec) {
            LogPrint(eLogWarning, "StreamMonitor: Stream [", streamId.first, ",", streamId.second, 
                     "] inactive for ", timeSinceActivity, " seconds");
            health.isStable = false;
        }
    }
}

void StreamStabilityMonitor::checkStreamHealth(StreamID streamId, StreamHealth& health) {
    if (!health.isHealthy() && m_failureCallback) {
        m_failureCallback(streamId, health);
    }
}

void StreamStabilityMonitor::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    LogPrint(eLogInfo, "StreamStabilityMonitor: Shutting down monitoring system...");
    
    // Clear all monitored streams and callbacks
    m_streams.clear();
    m_failureCallback = nullptr;
    
    LogPrint(eLogInfo, "StreamStabilityMonitor: Monitoring system shutdown complete");
}

// StreamMonitorGuard Implementation

StreamMonitorGuard::StreamMonitorGuard(StreamStabilityMonitor::StreamID streamId)
    : m_streamId(streamId), m_registered(false) {
    StreamStabilityMonitor::getInstance().registerStream(m_streamId);
    m_registered = true;
}

StreamMonitorGuard::~StreamMonitorGuard() {
    if (m_registered) {
        StreamStabilityMonitor::getInstance().unregisterStream(m_streamId);
    }
}

void StreamMonitorGuard::recordActivity() {
    if (m_registered) {
        StreamStabilityMonitor::getInstance().updateStreamActivity(m_streamId);
    }
}

void StreamMonitorGuard::recordResend() {
    if (m_registered) {
        StreamStabilityMonitor::getInstance().recordResend(m_streamId);
    }
}

void StreamMonitorGuard::recordTunnelSwitch() {
    if (m_registered) {
        StreamStabilityMonitor::getInstance().recordTunnelSwitch(m_streamId);
    }
}

void StreamMonitorGuard::recordThroughput(double kbps) {
    if (m_registered) {
        StreamStabilityMonitor::getInstance().recordThroughput(m_streamId, kbps);
    }
}

void StreamMonitorGuard::markUnstable() {
    if (m_registered) {
        StreamStabilityMonitor::getInstance().markStreamUnstable(m_streamId);
    }
}

bool StreamMonitorGuard::isHealthy() const {
    if (m_registered) {
        return StreamStabilityMonitor::getInstance().isStreamHealthy(m_streamId);
    }
    return false;
}

bool StreamMonitorGuard::requiresRecovery() const {
    if (m_registered) {
        return StreamStabilityMonitor::getInstance().streamRequiresRecovery(m_streamId);
    }
    return true;
}

} // namespace i2p::core