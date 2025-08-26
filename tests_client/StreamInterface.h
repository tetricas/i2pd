#pragma once

#include <memory>
#include <string>
#include <functional>
#include "Streaming.h"


namespace i2p::embed
{

/**
 * @brief Abstract interface for client/server communication patterns
 */
class IStreamClient
{
public:
    virtual ~IStreamClient() = default;
    
    /**
     * @brief Connect to a server and send a message
     * @param serverB32 Server's base32 address
     * @param message Message to send
     * @param timeout_ms Timeout in milliseconds
     * @return Response from server, empty if failed
     */
    virtual std::string sendMessage(const std::string& serverB32, 
                                    const std::string& message,
                                    int timeout_ms = 10000) = 0;
    
    /**
     * @brief Check if client is ready for communication
     */
    [[nodiscard]] virtual bool isReady() const = 0;
    
    /**
     * @brief Get client status/diagnostic info
     */
    [[nodiscard]] virtual std::string getStatus() const = 0;
};

/**
 * @brief Abstract interface for server communication patterns
 */
class IStreamServer
{
public:
    virtual ~IStreamServer() = default;
    
    /**
     * @brief Message handler function type
     * @param clientMessage Message received from client
     * @return Response to send back to client
     */
    using MessageHandler = std::function<std::string(const std::string& clientMessage)>;
    
    /**
     * @brief Start server and begin accepting connections
     * @param handler Function to handle incoming messages
     */
    virtual void start(MessageHandler handler) = 0;
    
    /**
     * @brief Stop server
     */
    virtual void stop() = 0;
    
    /**
     * @brief Get server's base32 address
     */
    [[nodiscard]] virtual std::string getB32Address() const = 0;
    
    /**
     * @brief Check if server is ready to accept connections
     */
    [[nodiscard]] virtual bool isReady() const = 0;
    
    /**
     * @brief Get server status/diagnostic info
     */
    [[nodiscard]] virtual std::string getStatus() const = 0;
};

/**
 * @brief Factory for creating client/server instances
 */
class StreamFactory
{
public:
    enum class ProtocolType{
        NORMAL_STREAMING,    // Standard i2pd streaming protocol
        SIMPLE_MESSAGING     // SimpleSend/SimpleReceive protocol
    };
    
    /**
     * @brief Create a client instance
     * @param type Protocol type to use
     * @param destination Local destination for client
     */
    static std::unique_ptr<IStreamClient> createClient(ProtocolType type,
                                                       std::shared_ptr<i2p::client::ClientDestination> destination);
    
    /**
     * @brief Create a server instance  
     * @param type Protocol type to use
     * @param destination Local destination for server
     */
    static std::unique_ptr<IStreamServer> createServer(ProtocolType type,
                                                       std::shared_ptr<i2p::client::ClientDestination> destination);
};

} // namespace i2p::embed
