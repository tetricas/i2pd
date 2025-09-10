#pragma once

#include "ITransferStrategy.h"
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <functional>

namespace i2p::filetransfer
{

/**
 * @brief Registry for transfer strategies
 * Implements Registry Pattern + Strategy Pattern
 */
class TransferStrategyRegistry
{
public:
    /**
     * @brief Strategy creator function type
     */
    using StrategyCreator = std::function<std::unique_ptr<ITransferStrategy>()>;
    
    /**
     * @brief Get singleton instance
     */
    static TransferStrategyRegistry& instance();
    
    /**
     * @brief Register a strategy creator
     * @param strategyName Name of the strategy
     * @param creator Function that creates the strategy
     */
    void registerStrategy(const std::string& strategyName, StrategyCreator creator);
    
    /**
     * @brief Create a strategy by name
     * @param strategyName Strategy name
     * @return Unique pointer to strategy, or nullptr if not found
     */
    std::unique_ptr<ITransferStrategy> createStrategy(const std::string& strategyName);
    
    /**
     * @brief Get list of registered strategy names
     */
    std::vector<std::string> getRegisteredStrategies() const;
    
    /**
     * @brief Get strategies that support a specific protocol
     * @param protocolName Protocol name to check
     * @return Vector of strategy names that support the protocol
     */
    std::vector<std::string> getStrategiesForProtocol(const std::string& protocolName) const;
    
    /**
     * @brief Get best strategy for a protocol (first registered that supports it)
     * @param protocolName Protocol name
     * @return Strategy name, or empty string if none found
     */
    std::string getBestStrategyForProtocol(const std::string& protocolName) const;
    
    /**
     * @brief Check if a strategy is registered
     */
    bool isStrategyRegistered(const std::string& strategyName) const;
    
    /**
     * @brief Initialize with default strategies
     */
    void initializeDefaults();

private:
    TransferStrategyRegistry() = default;
    ~TransferStrategyRegistry() = default;
    
    // Singleton pattern - non-copyable
    TransferStrategyRegistry(const TransferStrategyRegistry&) = delete;
    TransferStrategyRegistry& operator=(const TransferStrategyRegistry&) = delete;
    
    std::map<std::string, StrategyCreator> m_strategies;
};

} // namespace i2p::filetransfer