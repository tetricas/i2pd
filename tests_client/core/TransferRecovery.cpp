#include "TransferRecovery.h"
#include "Log.h"
#include <iomanip>
#include <thread>
#include <chrono>

namespace i2p::core {

TransferRecovery& TransferRecovery::getInstance() {
    static TransferRecovery instance;
    return instance;
}

TransferRecovery::TransferRecovery() {
    // Set up stream failure monitoring
    StreamStabilityMonitor::getInstance().setFailureCallback(
        [this](StreamStabilityMonitor::StreamID streamId, const StreamHealth& health) {
            handleStreamFailure(streamId, health);
        });
}

void TransferRecovery::saveCheckpoint(const std::string& transferId, const TransferCheckpoint& checkpoint) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_checkpoints[transferId] = checkpoint;
    
    LogPrint(eLogInfo, "TransferRecovery: Saved checkpoint for ", transferId, 
             " - progress: ", std::fixed, std::setprecision(1), checkpoint.getProgress() * 100, "%");
}

bool TransferRecovery::loadCheckpoint(const std::string& transferId, TransferCheckpoint& checkpoint) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_checkpoints.find(transferId);
    if (it != m_checkpoints.end() && it->second.isValid()) {
        checkpoint = it->second;
        LogPrint(eLogInfo, "TransferRecovery: Loaded checkpoint for ", transferId, 
                 " - progress: ", std::fixed, std::setprecision(1), checkpoint.getProgress() * 100, "%");
        return true;
    }
    
    return false;
}

void TransferRecovery::clearCheckpoint(const std::string& transferId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_checkpoints.find(transferId);
    if (it != m_checkpoints.end()) {
        LogPrint(eLogInfo, "TransferRecovery: Cleared checkpoint for ", transferId);
        m_checkpoints.erase(it);
    }
}

void TransferRecovery::registerTransfer(const std::string& transferId, RecoveryCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_callbacks[transferId] = callback;
    
    LogPrint(eLogInfo, "TransferRecovery: Registered transfer ", transferId);
}

void TransferRecovery::unregisterTransfer(const std::string& transferId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_callbacks.erase(transferId);
    m_checkpoints.erase(transferId);
    
    // Remove stream associations
    for (auto it = m_streamToTransfer.begin(); it != m_streamToTransfer.end();) {
        if (it->second == transferId) {
            it = m_streamToTransfer.erase(it);
        } else {
            ++it;
        }
    }
    
    LogPrint(eLogInfo, "TransferRecovery: Unregistered transfer ", transferId);
}

void TransferRecovery::handleStreamFailure(StreamStabilityMonitor::StreamID streamId, const StreamHealth& health) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto streamIt = m_streamToTransfer.find(streamId);
    if (streamIt == m_streamToTransfer.end()) {
        LogPrint(eLogWarning, "TransferRecovery: Stream failure for unknown transfer");
        return;
    }
    
    std::string transferId = streamIt->second;
    
    auto checkpointIt = m_checkpoints.find(transferId);
    if (checkpointIt == m_checkpoints.end()) {
        LogPrint(eLogError, "TransferRecovery: No checkpoint found for failed transfer ", transferId);
        return;
    }
    
    TransferCheckpoint& checkpoint = checkpointIt->second;
    
    LogPrint(eLogError, "TransferRecovery: Stream failure detected for ", transferId, 
             " - resends:", health.resendCount, " tunnelSwitches:", health.tunnelSwitches);
    
    // Select recovery strategy based on failure type and progress
    RecoveryStrategy strategy = selectRecoveryStrategy(checkpoint, health);
    
    LogPrint(eLogInfo, "TransferRecovery: Selected recovery strategy: ", (int)strategy);
    
    // Attempt recovery
    if (shouldRetry(checkpoint)) {
        checkpoint.attemptCount++;
        
        if (strategy == RecoveryStrategy::DELAYED_RETRY) {
            scheduleDelayedRecovery(transferId, m_retryDelaySec);
        } else {
            std::thread([this, transferId, strategy]() {
                attemptRecovery(transferId, strategy);
            }).detach();
        }
    } else {
        LogPrint(eLogError, "TransferRecovery: Max retry attempts reached for ", transferId);
    }
}

bool TransferRecovery::attemptRecovery(const std::string& transferId, RecoveryStrategy strategy) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto callbackIt = m_callbacks.find(transferId);
    if (callbackIt == m_callbacks.end()) {
        LogPrint(eLogError, "TransferRecovery: No callback for transfer ", transferId);
        return false;
    }
    
    auto checkpointIt = m_checkpoints.find(transferId);
    if (checkpointIt == m_checkpoints.end()) {
        LogPrint(eLogError, "TransferRecovery: No checkpoint for transfer ", transferId);
        return false;
    }
    
    LogPrint(eLogInfo, "TransferRecovery: Attempting recovery for ", transferId, 
             " using strategy ", (int)strategy);
    
    try {
        return callbackIt->second(checkpointIt->second, strategy);
    } catch (const std::exception& e) {
        LogPrint(eLogError, "TransferRecovery: Recovery callback exception: ", e.what());
        return false;
    }
}

RecoveryStrategy TransferRecovery::selectRecoveryStrategy(const TransferCheckpoint& checkpoint, 
                                                         const StreamHealth& health) {
    // High resend count suggests network issues - use delayed retry
    if (health.resendCount >= 5) {
        return RecoveryStrategy::DELAYED_RETRY;
    }
    
    // Multiple tunnel switches suggest infrastructure problems - try chunked recovery
    if (health.tunnelSwitches >= 3) {
        return RecoveryStrategy::CHUNKED_RECOVERY;
    }
    
    // If we have significant progress, try checkpoint resume
    if (checkpoint.getProgress() > 0.2) { // More than 20% completed
        return RecoveryStrategy::CHECKPOINT_RESUME;
    }
    
    // Low throughput suggests protocol issues - try immediate retry first
    if (health.throughputKBps < 100.0) {
        if (checkpoint.attemptCount < 2) {
            return RecoveryStrategy::IMMEDIATE_RETRY;
        } else {
            return RecoveryStrategy::PROTOCOL_SWITCH;
        }
    }
    
    // Default to immediate retry for first attempt
    return RecoveryStrategy::IMMEDIATE_RETRY;
}

bool TransferRecovery::shouldRetry(const TransferCheckpoint& checkpoint) const {
    return checkpoint.attemptCount < m_maxRetryAttempts;
}

void TransferRecovery::scheduleDelayedRecovery(const std::string& transferId, int delaySec) {
    LogPrint(eLogInfo, "TransferRecovery: Scheduling delayed recovery for ", transferId, 
             " in ", delaySec, " seconds");
    
    std::thread([this, transferId, delaySec]() {
        std::this_thread::sleep_for(std::chrono::seconds(delaySec));
        attemptRecovery(transferId, RecoveryStrategy::CHECKPOINT_RESUME);
    }).detach();
}

void TransferRecovery::associateStreamWithTransfer(StreamStabilityMonitor::StreamID streamId, const std::string& transferId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_streamToTransfer[streamId] = transferId;
}

void TransferRecovery::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    LogPrint(eLogInfo, "TransferRecovery: Shutting down recovery system...");
    
    // Clear all active transfers and checkpoints
    m_callbacks.clear();
    m_checkpoints.clear();
    m_streamToTransfer.clear();
    
    LogPrint(eLogInfo, "TransferRecovery: Recovery system shutdown complete");
}

// TransferRecoveryGuard Implementation

TransferRecoveryGuard::TransferRecoveryGuard(const std::string& transferId, 
                                           TransferRecovery::RecoveryCallback callback)
    : m_transferId(transferId), m_registered(false) {
    TransferRecovery::getInstance().registerTransfer(m_transferId, callback);
    m_registered = true;
}

TransferRecoveryGuard::~TransferRecoveryGuard() {
    if (m_registered) {
        TransferRecovery::getInstance().unregisterTransfer(m_transferId);
    }
}

void TransferRecoveryGuard::saveCheckpoint(const TransferCheckpoint& checkpoint) {
    if (m_registered) {
        TransferRecovery::getInstance().saveCheckpoint(m_transferId, checkpoint);
    }
}

bool TransferRecoveryGuard::loadCheckpoint(TransferCheckpoint& checkpoint) {
    if (m_registered) {
        return TransferRecovery::getInstance().loadCheckpoint(m_transferId, checkpoint);
    }
    return false;
}

void TransferRecoveryGuard::associateStream(StreamStabilityMonitor::StreamID streamId) {
    if (m_registered) {
        TransferRecovery::getInstance().associateStreamWithTransfer(streamId, m_transferId);
    }
}

} // namespace i2p::core