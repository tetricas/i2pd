#pragma once

#include "IStreamingProvider.h"
#include "NormalStreamingImpl.h"
#include <memory>

namespace i2p::transport
{

/**
 * @brief Normal Streaming implementation of message provider
 * Concrete implementation for Dependency Inversion
 */
class NormalStreamingMessageProvider : public IStreamingMessageProvider
{
private:
    std::unique_ptr<embed::NormalStreamClient> m_impl;
    
public:
    explicit NormalStreamingMessageProvider(std::shared_ptr<client::ClientDestination> destination);
    ~NormalStreamingMessageProvider() override = default;
    
    // IStreamingMessageProvider interface
    std::string sendMessage(const std::string& serverB32,
                           const std::string& message,
                           int timeout_ms) override;
    
    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] std::string getStatus() const override;
};

/**
 * @brief Normal Streaming implementation of server provider
 * Concrete implementation for Dependency Inversion
 */
class NormalStreamingServerProvider : public IStreamingServerProvider
{
private:
    std::unique_ptr<embed::NormalStreamServer> m_impl;
    
public:
    explicit NormalStreamingServerProvider(std::shared_ptr<client::ClientDestination> destination);
    ~NormalStreamingServerProvider() override = default;
    
    // IStreamingServerProvider interface
    void start(const MessageHandler handler) override;
    void stop() override;
    
    [[nodiscard]] std::string getB32Address() const override;
    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] std::string getStatus() const override;
};

/**
 * @brief Factory for Normal Streaming providers
 * Concrete factory implementing Abstract Factory pattern
 */
class NormalStreamingProviderFactory : public IStreamingProviderFactory
{
public:
    ~NormalStreamingProviderFactory() override = default;
    
    // IStreamingProviderFactory interface
    std::unique_ptr<IStreamingMessageProvider> createMessageProvider(
        std::shared_ptr<client::ClientDestination> destination) override;
    
    std::unique_ptr<IStreamingServerProvider> createServerProvider(
        std::shared_ptr<client::ClientDestination> destination) override;
};

} // namespace i2p::transport