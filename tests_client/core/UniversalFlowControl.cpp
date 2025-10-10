#include "UniversalFlowControl.h"
#include "FileTransferLogging.h"
#include <thread>
#include <algorithm>

namespace i2p::filetransfer {

// Static member definitions
int UniversalFlowControl::m_currentDelayUs = UNIVERSAL_DELAY_US;
int UniversalFlowControl::m_currentMaxRate = SAFE_DEFAULT_RATE;
bool UniversalFlowControl::m_adaptiveMode = true;
std::chrono::steady_clock::time_point UniversalFlowControl::m_lastConfigUpdate = {};

void UniversalFlowControl::EnforceFlowControl()
{
    if (m_adaptiveMode) {
        // Check if we need to update configuration based on network changes
        auto now = std::chrono::steady_clock::now();
        auto timeSinceUpdate = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastConfigUpdate).count();
        
        if (timeSinceUpdate > 30 || m_lastConfigUpdate.time_since_epoch().count() == 0) {
            AdaptDelayToConditions();
        }
    }
    
    // Apply the current flow control delay
    if (m_currentDelayUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(m_currentDelayUs));
    }
}

int UniversalFlowControl::GetOptimalMessageRate()
{
    return m_currentMaxRate;
}

void UniversalFlowControl::ConfigureForNetwork(const NetworkProfile& profile)
{
    int newDelay = CalculateOptimalDelay(profile);
    
    if (newDelay != m_currentDelayUs) {
        FT_LOG_INFO("UniversalFlowControl", "Updating flow control: " << m_currentDelayUs << "μs → " << newDelay << "μs" <<
                   " (RTT=" << profile.average_rtt_ms << "ms, Local=" << (profile.is_local_network ? "yes" : "no") << ")");
        
        m_currentDelayUs = newDelay;
        
        // Update message rate based on delay
        if (profile.is_optimal_conditions) {
            m_currentMaxRate = OPTIMAL_MAX_RATE;
        } else if (profile.is_local_network) {
            m_currentMaxRate = (OPTIMAL_MAX_RATE + SAFE_DEFAULT_RATE) / 2; // Middle ground
        } else {
            m_currentMaxRate = SAFE_DEFAULT_RATE;
        }
    }
    
    m_lastConfigUpdate = std::chrono::steady_clock::now();
}

void UniversalFlowControl::SetAdaptiveMode(bool enabled)
{
    if (m_adaptiveMode != enabled) {
        FT_LOG_INFO("UniversalFlowControl", "Adaptive mode " << (enabled ? "enabled" : "disabled"));
        m_adaptiveMode = enabled;
        
        if (!enabled) {
            // Revert to universal safe defaults
            m_currentDelayUs = UNIVERSAL_DELAY_US;
            m_currentMaxRate = SAFE_DEFAULT_RATE;
        } else {
            // Force immediate adaptation
            AdaptDelayToConditions();
        }
    }
}

int UniversalFlowControl::GetCurrentDelayUs()
{
    return m_currentDelayUs;
}

void UniversalFlowControl::RefreshConfiguration()
{
    if (m_adaptiveMode) {
        AdaptDelayToConditions();
    }
}

void UniversalFlowControl::AdaptDelayToConditions()
{
    auto profile = NetworkDetector::GetCurrentProfile();
    ConfigureForNetwork(profile);
}

int UniversalFlowControl::CalculateOptimalDelay(const NetworkProfile& profile)
{
    if (profile.is_optimal_conditions) {
        // Ideal network: minimal delay for maximum performance
        return AGGRESSIVE_DELAY_US;
    } else if (profile.is_local_network) {
        // Local network: slightly more conservative but still fast
        return UNIVERSAL_DELAY_US / 2; // 25μs
    } else if (profile.average_rtt_ms > 100) {
        // High latency network: more conservative delay
        return CONSERVATIVE_DELAY_US;
    } else {
        // Standard internet: universal default
        return UNIVERSAL_DELAY_US;
    }
}

} // namespace i2p::filetransfer