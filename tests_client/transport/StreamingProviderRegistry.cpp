#include "StreamingProviderRegistry.h"
#include "SimpleStreamingProvider.h"
#include "NormalStreamingProvider.h"
#include "../core/FileTransferLogging.h"

namespace i2p::transport
{

StreamingProviderRegistry& StreamingProviderRegistry::instance()
{
    static StreamingProviderRegistry instance;
    return instance;
}

void StreamingProviderRegistry::registerFactory(const std::string& protocolName, FactoryCreator creator)
{
    if (creator) {
        m_factories[protocolName] = std::move(creator);
        FT_LOG_DEBUG("StreamingProviderRegistry", "Registered factory for protocol: " << protocolName);
    } else {
        FT_LOG_WARNING("StreamingProviderRegistry", "Attempted to register null factory for: " << protocolName);
    }
}

std::unique_ptr<IStreamingProviderFactory> StreamingProviderRegistry::createFactory(const std::string& protocolName)
{
    auto it = m_factories.find(protocolName);
    if (it != m_factories.end()) {
        FT_LOG_DEBUG("StreamingProviderRegistry", "Creating factory for protocol: " << protocolName);
        return it->second();
    }
    
    FT_LOG_ERROR("StreamingProviderRegistry", "No factory registered for protocol: " << protocolName);
    return nullptr;
}

std::vector<std::string> StreamingProviderRegistry::getRegisteredProtocols() const
{
    std::vector<std::string> protocols;
    protocols.reserve(m_factories.size());
    
    for (const auto& [name, _] : m_factories) {
        protocols.push_back(name);
    }
    
    return protocols;
}

bool StreamingProviderRegistry::isProtocolRegistered(const std::string& protocolName) const
{
    return m_factories.find(protocolName) != m_factories.end();
}

void StreamingProviderRegistry::initializeDefaults()
{
    FT_LOG_INFO("StreamingProviderRegistry", "Initializing default streaming provider factories");
    
    // Register Simple Streaming factory
    registerFactory("simple", []() -> std::unique_ptr<IStreamingProviderFactory> {
        return std::make_unique<SimpleStreamingProviderFactory>();
    });
    
    // Register Normal Streaming factory
    registerFactory("normal", []() -> std::unique_ptr<IStreamingProviderFactory> {
        return std::make_unique<NormalStreamingProviderFactory>();
    });
    
    FT_LOG_INFO("StreamingProviderRegistry", "Default factories registered: simple, normal");
}

} // namespace i2p::transport