#pragma once

#include "../core/TransferResult.h"
#include "Destination.h"
#include "Identity.h"
#include <memory>
#include <string>
#include <functional>

namespace i2p::transport
{

/**
 * @brief Abstract interface for streaming message providers
 * Implements Dependency Inversion Principle - high-level modules depend on this abstraction
 */
class IStreamingMessageProvider
{
public:
    virtual ~IStreamingMessageProvider() = default;
    
    /**
     * @brief Send a message and receive response
     * @param serverB32 Target server's base32 address  
     * @param message Message to send
     * @param timeout_ms Timeout in milliseconds
     * @return Response message or empty on failure
     */
    virtual std::string sendMessage(const std::string& serverB32,
                                   const std::string& message,
                                   int timeout_ms) = 0;
    
    /**
     * @brief Check if provider is ready for operations
     */
    [[nodiscard]] virtual bool isReady() const = 0;
    
    /**
     * @brief Get provider status
     */
    [[nodiscard]] virtual std::string getStatus() const = 0;
};

/**
 * @brief Abstract interface for streaming server providers  
 * Implements Dependency Inversion Principle
 */
class IStreamingServerProvider
{
public:
    virtual ~IStreamingServerProvider() = default;
    
    /**
     * @brief Message handler function type
     */
    using MessageHandler = std::function<std::string(const std::string& message, const i2p::data::IdentHash& clientHash)>;
    
    /**
     * @brief Start server with message handler
     * @param handler Function to handle incoming messages
     */
    virtual void start(const MessageHandler handler) = 0;
    
    /**
     * @brief Stop server
     */
    virtual void stop() = 0;
    
    /**
     * @brief Get server's base32 address
     */
    [[nodiscard]] virtual std::string getB32Address() const = 0;
    
    /**
     * @brief Check if server is ready
     */
    [[nodiscard]] virtual bool isReady() const = 0;
    
    /**
     * @brief Get server status
     */
    [[nodiscard]] virtual std::string getStatus() const = 0;
};

/**
 * @brief Abstract factory for creating streaming providers
 * Implements Abstract Factory Pattern + Dependency Inversion
 */
class IStreamingProviderFactory
{
public:
    virtual ~IStreamingProviderFactory() = default;
    
    /**
     * @brief Create a message client provider
     * @param destination Client destination
     * @return Unique pointer to message provider
     */
    virtual std::unique_ptr<IStreamingMessageProvider> createMessageProvider(
        std::shared_ptr<client::ClientDestination> destination) = 0;
    
    /**
     * @brief Create a server provider
     * @param destination Server destination  
     * @return Unique pointer to server provider
     */
    virtual std::unique_ptr<IStreamingServerProvider> createServerProvider(
        std::shared_ptr<client::ClientDestination> destination) = 0;
};

} // namespace i2p::transport