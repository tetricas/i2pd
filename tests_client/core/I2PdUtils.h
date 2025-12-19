#pragma once

#include <string>
#include <map>
#include <memory>
#include "Destination.h"

namespace i2p::embed {

/**
 * @brief Utility functions for i2pd initialization and management
 */
class I2PdUtils
{
public:
    /**
     * @brief Initialize i2pd node
     * @param mode Node mode ("server" or "client")
     * @param datadir Data directory path
     * @param config Configuration file path
     */
    static void initNode(const std::string& mode, 
                         const std::string& datadir,
                         const std::string& config);
    
    /**
     * @brief Start core i2pd services
     */
    static void startCore();
    
    /**
     * @brief Stop core i2pd services
     */
    static void stopCore();
    
    /**
     * @brief Create a local destination with tunnel parameters
     * @param isPublic Whether destination should be publicly announced
     * @param hops (optional)
     * @return Created destination
     */
    static std::shared_ptr<client::ClientDestination> createDestination(bool isPublic, int hops = 1);
    
    /**
     * @brief Verify NTCP2 is properly published in RouterInfo
     */
    static void verifyNTCP2Published();
    
    /**
     * @brief Initialize trust settings from configuration
     */
    static void initTrust();
    
    /**
     * @brief Convert base32 address to IdentHash
     * @param base32 Base32 address (without .b32.i2p suffix)
     * @return IdentHash or throws on invalid input
     */
    static data::IdentHash parseBase32(const std::string& base32);
    
    /**
     * @brief Wait for destination to become ready
     * @param destination Destination to wait for
     * @param timeout_ms Maximum time to wait in milliseconds
     * @return true if ready, false if timeout
     */
    static bool waitForDestinationReady(std::shared_ptr<client::ClientDestination> destination,
                                        int timeout_ms = 20000);
    
    /**
     * @brief Wait for LeaseSet to be available in NetDb
     * @param identHash Identity hash to look for
     * @param timeout_ms Maximum time to wait in milliseconds
     * @return true if found, false if timeout
     */
    static void waitForLeaseSet(const data::IdentHash& identHash,
                                int timeout_ms = 20000);
    
    /**
     * @brief Configure exploratory tunnel hop count
     * @param hopCount Number of hops for exploratory tunnels
     */
    static void configureExploratoryTunnels(int hopCount);
    
    /**
     * @brief Shutdown recovery systems before stopping core
     */
    static void shutdownRecoverySystems();
};

} // namespace i2p::embed
