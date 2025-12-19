#include "TransferStrategyRegistry.h"
#include "ITransferStrategy.h"
#include "../core/FileTransferLogging.h"

namespace i2p::filetransfer
{

TransferStrategyRegistry& TransferStrategyRegistry::instance()
{
    static TransferStrategyRegistry instance;
    return instance;
}

void TransferStrategyRegistry::registerStrategy(const std::string& strategyName, StrategyCreator creator)
{
    if (creator) {
        m_strategies[strategyName] = std::move(creator);
        FT_LOG_DEBUG("TransferStrategyRegistry", "Registered strategy: " << strategyName);
    } else {
        FT_LOG_WARNING("TransferStrategyRegistry", "Attempted to register null strategy creator for: " << strategyName);
    }
}

std::unique_ptr<ITransferStrategy> TransferStrategyRegistry::createStrategy(const std::string& strategyName)
{
    auto it = m_strategies.find(strategyName);
    if (it != m_strategies.end()) {
        FT_LOG_DEBUG("TransferStrategyRegistry", "Creating strategy: " << strategyName);
        return it->second();
    }
    
    FT_LOG_ERROR("TransferStrategyRegistry", "No strategy registered for: " << strategyName);
    return nullptr;
}

std::vector<std::string> TransferStrategyRegistry::getRegisteredStrategies() const
{
    std::vector<std::string> strategies;
    strategies.reserve(m_strategies.size());
    
    for (const auto& [name, _] : m_strategies) {
        strategies.push_back(name);
    }
    
    return strategies;
}

std::vector<std::string> TransferStrategyRegistry::getStrategiesForProtocol(const std::string& protocolName) const
{
    std::vector<std::string> supportedStrategies;
    
    for (const auto& [name, creator] : m_strategies) {
        // Create a temporary instance to check protocol support
        auto strategy = creator();
        if (strategy && strategy->supportsProtocol(protocolName)) {
            supportedStrategies.push_back(name);
        }
    }
    
    return supportedStrategies;
}

std::string TransferStrategyRegistry::getBestStrategyForProtocol(const std::string& protocolName) const
{
    auto strategies = getStrategiesForProtocol(protocolName);
    return strategies.empty() ? "" : strategies.front();
}

bool TransferStrategyRegistry::isStrategyRegistered(const std::string& strategyName) const
{
    return m_strategies.find(strategyName) != m_strategies.end();
}

void TransferStrategyRegistry::initializeDefaults()
{
    FT_LOG_INFO("TransferStrategyRegistry", "Initializing default transfer strategies");
    
    // Register chunked strategy
    registerStrategy("chunked", []() -> std::unique_ptr<ITransferStrategy> {
        return std::make_unique<ChunkedTransferStrategy>();
    });
    
    // Register binary streaming strategy  
    registerStrategy("binary-stream", []() -> std::unique_ptr<ITransferStrategy> {
        return std::make_unique<BinaryStreamingStrategy>();
    });
    
    // Register parallel strategy (even though not implemented yet)
    registerStrategy("parallel", []() -> std::unique_ptr<ITransferStrategy> {
        return std::make_unique<ParallelTransferStrategy>();
    });
    
    FT_LOG_INFO("TransferStrategyRegistry", "Default strategies registered: chunked, binary-stream, parallel");
}

} // namespace i2p::filetransfer