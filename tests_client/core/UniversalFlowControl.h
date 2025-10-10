#pragma once
#include "NetworkDetector.h"
#include <chrono>

namespace i2p::filetransfer {

/**
 * @brief Universal flow control system that adapts to network conditions
 * Provides robust flow control that works on all networks while optimizing for ideal conditions
 */
class UniversalFlowControl {
public:
    /**
     * @brief Enforce flow control based on current network conditions
     * This is the main function that should be called after each message/chunk transmission
     */
    static void EnforceFlowControl();
    
    /**
     * @brief Get optimal message rate for current network conditions
     * @return Recommended messages per second
     */
    static int GetOptimalMessageRate();
    
    /**
     * @brief Configure flow control for specific network profile
     * @param profile Network characteristics to optimize for
     */
    static void ConfigureForNetwork(const NetworkProfile& profile);
    
    /**
     * @brief Enable or disable adaptive optimization
     * @param enabled If true, flow control adapts to network conditions
     */
    static void SetAdaptiveMode(bool enabled);
    
    /**
     * @brief Get current flow control delay in microseconds
     * @return Current delay being applied
     */
    static int GetCurrentDelayUs();
    
    /**
     * @brief Force refresh of network-based configuration
     */
    static void RefreshConfiguration();
    
private:
    // Universal safe defaults that work on all networks
    static const int SAFE_DEFAULT_RATE = 500;    // msg/s - works everywhere
    static const int OPTIMAL_MAX_RATE = 2000;    // msg/s - only when ideal
    static const int UNIVERSAL_DELAY_US = 50;    // microseconds - baseline
    static const int AGGRESSIVE_DELAY_US = 10;   // microseconds - for optimal conditions
    static const int CONSERVATIVE_DELAY_US = 100; // microseconds - for poor conditions
    
    // Current configuration
    static int m_currentDelayUs;
    static int m_currentMaxRate;
    static bool m_adaptiveMode;
    static std::chrono::steady_clock::time_point m_lastConfigUpdate;
    
    /**
     * @brief Adapt delay to current network conditions
     */
    static void AdaptDelayToConditions();
    
    /**
     * @brief Calculate optimal delay for given network profile
     * @param profile Network characteristics
     * @return Delay in microseconds
     */
    static int CalculateOptimalDelay(const NetworkProfile& profile);
};

} // namespace i2p::filetransfer