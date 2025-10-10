#pragma once
#include <chrono>
#include <memory>

namespace i2p::filetransfer {

/**
 * @brief Network characteristics profile
 */
struct NetworkProfile {
    int average_rtt_ms;           // Round-trip time in milliseconds
    int available_bandwidth_kbps; // Available bandwidth in KB/s
    double packet_loss_rate;      // Packet loss percentage (0.0-1.0)
    bool is_local_network;        // <5ms RTT, <0.1% loss
    bool is_optimal_conditions;   // <2ms RTT, <0.01% loss
};

/**
 * @brief Network condition detector for adaptive protocol optimization
 * Provides universal network awareness for flow control and optimization
 */
class NetworkDetector {
public:
    /**
     * @brief Measure current network conditions
     * @return Comprehensive network profile
     */
    static NetworkProfile MeasureNetworkConditions();
    
    /**
     * @brief Check if network conditions are optimal for aggressive optimization
     * @return True if conditions allow maximum performance optimizations
     */
    static bool IsOptimalConditions();
    
    /**
     * @brief Check if this appears to be a local network environment
     * @return True if local network characteristics detected
     */
    static bool IsLocalNetwork();
    
    /**
     * @brief Get cached network profile (updated every 30 seconds)
     * @return Current cached network profile
     */
    static NetworkProfile GetCurrentProfile();
    
    /**
     * @brief Force refresh of network measurements
     */
    static void RefreshProfile();
    
private:
    /**
     * @brief Measure round-trip time to i2p routers
     * @return Average RTT in milliseconds
     */
    static int MeasureRTT();
    
    /**
     * @brief Estimate available bandwidth
     * @return Estimated bandwidth in KB/s
     */
    static int EstimateBandwidth();
    
    /**
     * @brief Measure packet loss rate
     * @return Packet loss rate (0.0-1.0)
     */
    static double MeasurePacketLoss();
    
    /**
     * @brief Update cached profile with fresh measurements
     */
    static void UpdateCachedProfile();
    
    // Cached data
    static NetworkProfile m_cachedProfile;
    static std::chrono::steady_clock::time_point m_lastUpdate;
    static const int CACHE_DURATION_SECONDS = 30;
    
    // Network condition thresholds
    static const int LOCAL_NETWORK_RTT_THRESHOLD = 5;      // ms
    static const int OPTIMAL_CONDITIONS_RTT_THRESHOLD = 2; // ms
    static const double LOCAL_NETWORK_LOSS_THRESHOLD;      // 0.001 (0.1%)
    static const double OPTIMAL_CONDITIONS_LOSS_THRESHOLD; // 0.0001 (0.01%)
};

} // namespace i2p::filetransfer