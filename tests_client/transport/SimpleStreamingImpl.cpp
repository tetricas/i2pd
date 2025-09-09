#include "SimpleStreamingImpl.h"
#include "../core/I2PdUtils.h"
#include "../core/FileTransferLogging.h"
#include "../core/TransferConfig.h"
#include "Log.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <utility>

using namespace std::chrono_literals;

namespace i2p::embed
{

// SimpleStreamClient Implementation

SimpleStreamClient::SimpleStreamClient(std::shared_ptr<client::ClientDestination> destination)
    : m_destination(std::move(destination)) {
}

std::string SimpleStreamClient::sendMessage(const std::string& serverB32, 
                                            const std::string& message,
                                            int timeout_ms)
{
    try {
        if (!isReady())
        {
            m_lastStatus = "Client destination not ready";
            return "";
        }
        
        // Parse server address
        auto serverHash = I2PdUtils::parseBase32(serverB32);
        
        // Optional prefetch
        m_destination->RequestDestination(serverHash);
        I2PdUtils::waitForLeaseSet(serverHash, i2p::filetransfer::TransferConfig::getLeaseSetTimeout());
        
        // Create stream
        auto stream = m_destination->CreateStream(serverHash);
        int maxRetries = i2p::filetransfer::TransferConfig::getMaxRetries();
        auto retryDelay = i2p::filetransfer::TransferConfig::getRetryDelayMs();
        for (int i = 0; i < maxRetries && !stream; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(retryDelay));
            stream = m_destination->CreateStream(serverHash);
        }
        
        if (!stream)
        {
            m_lastStatus = "Failed to create stream";
            FT_LOG_ERROR("SimpleClient", "CreateStream failed after " << i2p::filetransfer::TransferConfig::getMaxRetries() << " retries");
            return "";
        }
        
        FT_LOG_DEBUG("SimpleClient", "Stream created, status: " << stream->GetStatus());
        
        // Wait for stream to establish connection - but SimpleSend works even without establishment
        for (int i = 0; i < 50 && !stream->IsEstablished(); ++i)
        {
            std::this_thread::sleep_for(100ms);
        }
        
        FT_LOG_DEBUG("SimpleClient", "Stream established: " << stream->IsEstablished());
        FT_LOG_DEBUG("SimpleClient", "Stream status: " << stream->GetStatus());
        FT_LOG_DEBUG("SimpleClient", "Sending message via SendEcho: [" << message << "]");
        
        // Use SendEcho for reliable request-response (this uses ping protocol)
        stream->SimpleSend(message, true); // true = expect response
        
        FT_LOG_DEBUG("SimpleClient", "SendEcho sent, waiting for response...");
        
        // Use ReceiveEcho to get the response
        FT_LOG_DEBUG("SimpleClient", "Calling SimpleReceive with timeout " << (timeout_ms / 2) << "ms");
        std::string response = stream->SimpleReceive(timeout_ms / 2);
        FT_LOG_DEBUG("SimpleClient", "SimpleReceive returned: [" << response << "], size=" << response.size());
        
        if (!response.empty())
        {
            FT_LOG_DEBUG("SimpleClient", "ReceiveEcho success, got: [" << response << "]");
            m_lastStatus = "Message exchange successful";
            return response;
        }
        else
        {
            m_lastStatus = "ReceiveEcho timeout or failed";
            FT_LOG_WARN("SimpleClient", "ReceiveEcho timeout or failed");
            return "";
        }
        
    }
    catch (const std::exception& e)
    {
        m_lastStatus = "Exception: " + std::string(e.what());
        FT_LOG_ERROR("SimpleClient", "Exception: " << e.what());
        return "";
    }
}

bool SimpleStreamClient::isReady() const
{
    return m_destination && m_destination->IsReady();
}

std::string SimpleStreamClient::getStatus() const
{
if (!m_destination)
        return "No destination";
    
    std::ostringstream oss;
    oss << "Destination ready: " << (m_destination->IsReady() ? "yes" : "no");
    if (!m_lastStatus.empty())
        oss << ", Last operation: " << m_lastStatus;

    return oss.str();
}

// SimpleStreamServer Implementation

SimpleStreamServer::SimpleStreamServer(std::shared_ptr<client::ClientDestination> destination)
    : m_destination(std::move(destination)) {
}

SimpleStreamServer::~SimpleStreamServer() {
    SimpleStreamServer::stop();
}

void SimpleStreamServer::start(const MessageHandler handler)
{
    if (m_running.load())
    {
        FT_LOG_WARN("SimpleServer", "Already running");
        return;
    }
    
    m_handler = handler;
    m_running.store(true);
    
    // Wait for destination to be ready and published
    if (!I2PdUtils::waitForDestinationReady(m_destination, i2p::filetransfer::TransferConfig::getConnectionTimeout()))
    {
        m_lastStatus = "Destination failed to become ready";
        FT_LOG_ERROR("SimpleServer", "Destination not ready");
        return;
    }
    
    // Extra safety: wait until NetDb can return our LeaseSet
    I2PdUtils::waitForLeaseSet(m_destination->GetIdentHash(), i2p::filetransfer::TransferConfig::getLeaseSetTimeout());
    
    FT_LOG_INFO("SimpleServer", "Server ready, b32: " << getB32Address());
    
    // Set simple message handler on the streaming destination
    auto streamingDest = m_destination->GetStreamingDestination();
    if (!streamingDest) {
        FT_LOG_ERROR("SimpleServer", "Failed to get streaming destination");
        m_lastStatus = "Failed to get streaming destination";
        return;
    }
    
    streamingDest->SetSimpleMessageHandler([this](const std::string& message, const i2p::data::IdentHash& clientHash) -> std::string {
        FT_LOG_DEBUG("SimpleServer", "Simple message handler called with: [" << message << "] from client: " << clientHash.ToBase32().substr(0, 8) << "...");
        if (m_handler) {
            std::string response = m_handler(message, clientHash);
            FT_LOG_DEBUG("SimpleServer", "Handler returned: [" << response << "]");
            return response;
        }

        FT_LOG_DEBUG("SimpleServer", "No handler set, using echo");
        return "echo: " + message;
    });
    
    FT_LOG_DEBUG("SimpleServer", "Simple message handler installed");

    m_lastStatus = "Server started and accepting simple messages";
}

void SimpleStreamServer::stop()
{
    if (m_running.load())
    {
        m_running.store(false);
        
        // Reset simple message handler
        if ( m_destination->ResetSimpleMessageHandler())
            FT_LOG_DEBUG("SimpleServer", "Simple message handler reset");
        
        m_lastStatus = "Server stopped";
        FT_LOG_INFO("SimpleServer", "Server stopped");
    }
}

std::string SimpleStreamServer::getB32Address() const
{
    if (m_destination)
        return m_destination->GetIdentHash().ToBase32() + ".b32.i2p";

    return "";
}

bool SimpleStreamServer::isReady() const
{
    return m_destination && m_destination->IsReady() && m_running.load();
}

std::string SimpleStreamServer::getStatus() const
{
    if (!m_destination)
        return "No destination";
    
    std::ostringstream oss;
    oss << "Running: " << (m_running.load() ? "yes" : "no")
        << ", Destination ready: " << (m_destination->IsReady() ? "yes" : "no");
    if (!m_lastStatus.empty())
        oss << ", Status: " << m_lastStatus;

    return oss.str();
}


} // namespace i2p::embed
