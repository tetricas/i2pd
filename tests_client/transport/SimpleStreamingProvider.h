#pragma once

#include "IStreamingProvider.h"
#include "SimpleStreamingImpl.h"
#include <memory>

namespace i2p::transport
{

/**
 * @brief Simple Streaming implementation of message provider
 * Concrete implementation for Dependency Inversion
 */
class SimpleStreamingMessageProvider : public IStreamingMessageProvider
{
private:
    std::unique_ptr<embed::SimpleStreamClient> m_impl;
    
public:
    explicit SimpleStreamingMessageProvider(std::shared_ptr<client::ClientDestination> destination);
    ~SimpleStreamingMessageProvider() override = default;
    
    // IStreamingMessageProvider interface
    std::string sendMessage(const std::string& serverB32,
                           const std::string& message,
                           int timeout_ms) override;
    
    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] std::string getStatus() const override;
};

/**
 * @brief Simple Streaming implementation of server provider
 * Concrete implementation for Dependency Inversion
 */
class SimpleStreamingServerProvider : public IStreamingServerProvider
{
private:
    std::unique_ptr<embed::SimpleStreamServer> m_impl;
    
public:
    explicit SimpleStreamingServerProvider(std::shared_ptr<client::ClientDestination> destination);
    ~SimpleStreamingServerProvider() override = default;
    
    // IStreamingServerProvider interface
    void start(const MessageHandler handler) override;
    void stop() override;
    
    [[nodiscard]] std::string getB32Address() const override;
    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] std::string getStatus() const override;
};

/**
 * @brief Factory for Simple Streaming providers
 * Concrete factory implementing Abstract Factory pattern
 */
class SimpleStreamingProviderFactory : public IStreamingProviderFactory
{
public:
    ~SimpleStreamingProviderFactory() override = default;
    
    // IStreamingProviderFactory interface
    std::unique_ptr<IStreamingMessageProvider> createMessageProvider(
        std::shared_ptr<client::ClientDestination> destination) override;
    
    std::unique_ptr<IStreamingServerProvider> createServerProvider(
        std::shared_ptr<client::ClientDestination> destination) override;
};

} // namespace i2p::transport