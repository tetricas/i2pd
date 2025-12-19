#pragma once

#include "IStreamingProvider.h"
#include <memory>
#include <string>
#include <map>
#include <functional>

namespace i2p::transport
{

/**
 * @brief Registry for streaming provider factories
 * Implements Registry Pattern + Dependency Inversion Principle
 */
class StreamingProviderRegistry
{
public:
    /**
     * @brief Factory creation function type
     */
    using FactoryCreator = std::function<std::unique_ptr<IStreamingProviderFactory>()>;
    
    /**
     * @brief Get singleton instance
     */
    static StreamingProviderRegistry& instance();
    
    /**
     * @brief Register a factory creator for a protocol type
     * @param protocolName Name of the protocol (e.g., "simple", "normal")
     * @param creator Function that creates the factory
     */
    void registerFactory(const std::string& protocolName, FactoryCreator creator);
    
    /**
     * @brief Create a factory for the specified protocol
     * @param protocolName Protocol name
     * @return Unique pointer to factory, or nullptr if not found
     */
    std::unique_ptr<IStreamingProviderFactory> createFactory(const std::string& protocolName);
    
    /**
     * @brief Get list of registered protocol names
     */
    std::vector<std::string> getRegisteredProtocols() const;
    
    /**
     * @brief Check if a protocol is registered
     */
    bool isProtocolRegistered(const std::string& protocolName) const;
    
    /**
     * @brief Initialize with default protocols
     * Registers Simple and Normal streaming factories
     */
    void initializeDefaults();

private:
    StreamingProviderRegistry() = default;
    ~StreamingProviderRegistry() = default;
    
    // Singleton pattern - non-copyable
    StreamingProviderRegistry(const StreamingProviderRegistry&) = delete;
    StreamingProviderRegistry& operator=(const StreamingProviderRegistry&) = delete;
    
    std::map<std::string, FactoryCreator> m_factories;
};

} // namespace i2p::transport