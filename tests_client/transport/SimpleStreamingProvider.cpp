#include "SimpleStreamingProvider.h"
#include "../core/FileTransferLogging.h"

namespace i2p::transport
{

// SimpleStreamingMessageProvider Implementation

SimpleStreamingMessageProvider::SimpleStreamingMessageProvider(std::shared_ptr<client::ClientDestination> destination)
    : m_impl(std::make_unique<embed::SimpleStreamClient>(destination))
{
    FT_LOG_DEBUG("SimpleStreamingProvider", "Message provider created");
}

std::string SimpleStreamingMessageProvider::sendMessage(const std::string& serverB32,
                                                        const std::string& message,
                                                        int timeout_ms)
{
    if (!m_impl) {
        FT_LOG_ERROR("SimpleStreamingProvider", "Implementation not available");
        return "";
    }
    
    return m_impl->sendMessage(serverB32, message, timeout_ms);
}

bool SimpleStreamingMessageProvider::isReady() const
{
    return m_impl && m_impl->isReady();
}

std::string SimpleStreamingMessageProvider::getStatus() const
{
    return m_impl ? m_impl->getStatus() : "No implementation";
}

// SimpleStreamingServerProvider Implementation

SimpleStreamingServerProvider::SimpleStreamingServerProvider(std::shared_ptr<client::ClientDestination> destination)
    : m_impl(std::make_unique<embed::SimpleStreamServer>(destination))
{
    FT_LOG_DEBUG("SimpleStreamingProvider", "Server provider created");
}

void SimpleStreamingServerProvider::start(const MessageHandler handler)
{
    if (!m_impl) {
        FT_LOG_ERROR("SimpleStreamingProvider", "Server implementation not available");
        return;
    }
    
    m_impl->start(handler);
}

void SimpleStreamingServerProvider::stop()
{
    if (m_impl) {
        m_impl->stop();
    }
}

std::string SimpleStreamingServerProvider::getB32Address() const
{
    return m_impl ? m_impl->getB32Address() : "";
}

bool SimpleStreamingServerProvider::isReady() const
{
    return m_impl && m_impl->isReady();
}

std::string SimpleStreamingServerProvider::getStatus() const
{
    return m_impl ? m_impl->getStatus() : "No implementation";
}

// SimpleStreamingProviderFactory Implementation

std::unique_ptr<IStreamingMessageProvider> SimpleStreamingProviderFactory::createMessageProvider(
    std::shared_ptr<client::ClientDestination> destination)
{
    return std::make_unique<SimpleStreamingMessageProvider>(destination);
}

std::unique_ptr<IStreamingServerProvider> SimpleStreamingProviderFactory::createServerProvider(
    std::shared_ptr<client::ClientDestination> destination)
{
    return std::make_unique<SimpleStreamingServerProvider>(destination);
}

} // namespace i2p::transport