#pragma once

#include "StreamInterface.h"
#include <atomic>
#include <memory>


namespace i2p::embed {

/**
 * @brief Client implementation using SimpleSend/SimpleReceive protocol
 */
class SimpleStreamClient final : public IStreamClient
{
    std::shared_ptr<client::ClientDestination> m_destination;
    mutable std::string m_lastStatus;

public:
    explicit SimpleStreamClient(std::shared_ptr<client::ClientDestination> destination);
    
    std::string sendMessage(const std::string& serverB32, 
                            const std::string& message,
                            int timeout_ms = 10000) override;
    
    bool isReady() const override;
    std::string getStatus() const override;
};

/**
 * @brief Server implementation using SimpleSend/SimpleReceive protocol
 */
class SimpleStreamServer : public IStreamServer
{
    std::shared_ptr<client::ClientDestination> m_destination;
    MessageHandler m_handler;
    std::atomic<bool> m_running{false};
    mutable std::string m_lastStatus;

public:
    explicit SimpleStreamServer(std::shared_ptr<client::ClientDestination> destination);
    ~SimpleStreamServer() override;
    
    void start(MessageHandler handler) override;
    void stop() override;
    std::string getB32Address() const override;
    bool isReady() const override;
    std::string getStatus() const override;

private:
    /**
     * @brief Handle incoming stream connection
     */
    void handleIncomingStream(std::shared_ptr<stream::Stream> stream);
};

} // namespace i2p::embed
