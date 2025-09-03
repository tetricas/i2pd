#pragma once

#include "StreamInterface.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <mutex>
#include <condition_variable>


namespace i2p::embed {

/**
 * @brief Client implementation using standard i2pd streaming protocol
 */
class NormalStreamClient final : public IStreamClient
{
    std::shared_ptr<client::ClientDestination> m_destination;
    mutable std::string m_lastStatus;
    
    // For handling server response streams
    std::mutex m_responseMutex;
    std::condition_variable m_responseCondition;
    std::string m_receivedResponse;
    bool m_expectingResponse;

public:
    explicit NormalStreamClient(std::shared_ptr<client::ClientDestination> destination);
    
    std::string sendMessage(const std::string& serverB32, 
                           const std::string& message, 
                           int timeout_ms = 10000) override;
    
    bool isReady() const override;
    std::string getStatus() const override;

private:
    /**
     * @brief Receive data from stream with retries
     */
    std::string receiveFromStream(std::shared_ptr<stream::Stream> stream, int timeout_ms);
    
    /**
     * @brief Handle incoming stream from server (for response)
     */
    void handleIncomingServerStream(std::shared_ptr<stream::Stream> stream);
};

/**
 * @brief Server implementation using standard i2pd streaming protocol  
 */
class NormalStreamServer : public IStreamServer
{
    std::shared_ptr<client::ClientDestination> m_destination;
    MessageHandler m_handler;
    std::atomic<bool> m_running{false};
    mutable std::string m_lastStatus;

public:
    explicit NormalStreamServer(std::shared_ptr<client::ClientDestination> destination);
    ~NormalStreamServer() override;
    
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
    
    /**
     * @brief Receive loop for processing stream data
     */
    void receiveLoop(std::shared_ptr<stream::Stream> stream);
};

} // namespace i2p::embed
