#include "NormalStreamingProvider.h"
#include "../core/FileTransferLogging.h"

namespace i2p::transport
{

// NormalStreamingMessageProvider Implementation

NormalStreamingMessageProvider::NormalStreamingMessageProvider(std::shared_ptr<client::ClientDestination> destination)
    : m_impl(std::make_unique<embed::NormalStreamClient>(destination))
{
    FT_LOG_DEBUG("NormalStreamingProvider", "Message provider created");
}

std::string NormalStreamingMessageProvider::sendMessage(const std::string& serverB32,
                                                        const std::string& message,
                                                        int timeout_ms)
{
    if (!m_impl) {
        FT_LOG_ERROR("NormalStreamingProvider", "Implementation not available");
        return "";
    }
    
    return m_impl->sendMessage(serverB32, message, timeout_ms);
}

bool NormalStreamingMessageProvider::isReady() const
{
    return m_impl && m_impl->isReady();
}

std::string NormalStreamingMessageProvider::getStatus() const
{
    return m_impl ? m_impl->getStatus() : "No implementation";
}

// NormalStreamingServerProvider Implementation

NormalStreamingServerProvider::NormalStreamingServerProvider(std::shared_ptr<client::ClientDestination> destination)
    : m_impl(std::make_unique<embed::NormalStreamServer>(destination))
{
    FT_LOG_DEBUG("NormalStreamingProvider", "Server provider created");
}

void NormalStreamingServerProvider::start(const MessageHandler handler)
{
    if (!m_impl) {
        FT_LOG_ERROR("NormalStreamingProvider", "Server implementation not available");
        return;
    }
    
    m_impl->start(handler);
}

void NormalStreamingServerProvider::stop()
{
    if (m_impl) {
        m_impl->stop();
    }
}

std::string NormalStreamingServerProvider::getB32Address() const
{
    return m_impl ? m_impl->getB32Address() : "";
}

bool NormalStreamingServerProvider::isReady() const
{
    return m_impl && m_impl->isReady();
}

std::string NormalStreamingServerProvider::getStatus() const
{
    return m_impl ? m_impl->getStatus() : "No implementation";
}

// NormalStreamingProviderFactory Implementation

std::unique_ptr<IStreamingMessageProvider> NormalStreamingProviderFactory::createMessageProvider(
    std::shared_ptr<client::ClientDestination> destination)
{
    return std::make_unique<NormalStreamingMessageProvider>(destination);
}

std::unique_ptr<IStreamingServerProvider> NormalStreamingProviderFactory::createServerProvider(
    std::shared_ptr<client::ClientDestination> destination)
{
    return std::make_unique<NormalStreamingServerProvider>(destination);
}

} // namespace i2p::transport