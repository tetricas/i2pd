#include "NormalStreamingImpl.h"
#include "I2PdUtils.h"
#include "Log.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <utility>

using namespace std::chrono_literals;


namespace i2p::embed {

// NormalStreamClient Implementation

NormalStreamClient::NormalStreamClient(std::shared_ptr<client::ClientDestination> destination)
    : m_destination(std::move(destination)) {
}

std::string NormalStreamClient::sendMessage(const std::string& serverB32, 
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
        I2PdUtils::waitForLeaseSet(serverHash, timeout_ms / 2);
        
        // Create stream
        auto stream = m_destination->CreateStream(serverHash);
        for (int i = 0; i < 50 && !stream; ++i)
        {
            std::this_thread::sleep_for(100ms);
            stream = m_destination->CreateStream(serverHash);
        }
        
        if (!stream)
        {
            m_lastStatus = "Failed to create stream";
            LogPrint(eLogError, "NormalClient: CreateStream failed");
            return "";
        }
        
        LogPrint(eLogInfo, "NormalClient: Stream status: ", stream->GetStatus());
        
        // Initial connect
        stream->Send(nullptr, 0);
        
        // Wait for establishment
        int establishWait = 0;
        while (!stream->IsEstablished() && 
               stream->GetStatus() != stream::eStreamStatusReset &&
               establishWait < 100)
        {
            std::this_thread::sleep_for(50ms);
            establishWait++;
        }
        
        if (!stream->IsEstablished())
        {
            m_lastStatus = "Stream failed to establish";
            LogPrint(eLogError, "NormalClient: Stream failed to establish");
            return "";
        }
        
        LogPrint(eLogInfo, "NormalClient: Stream established, sending message: [", message, "]");
        
        // Send message
        auto sent = stream->Send(reinterpret_cast<const uint8_t*>(message.data()), message.size());
        if (sent != message.size())
        {
            m_lastStatus = "Partial send: " + std::to_string(sent) + "/" + std::to_string(message.size());
            LogPrint(eLogError, "NormalClient: Partial send: ", sent, "/", message.size());
            return "";
        }
        
        // Wait for transmission
        std::this_thread::sleep_for(2000ms);
        
        if (!stream->IsOpen())
        {
            m_lastStatus = "Stream closed after send";
            LogPrint(eLogError, "NormalClient: Stream is not open after send");
            return "";
        }
        
        // Receive response
        std::string response = receiveFromStream(stream, timeout_ms);
        
        stream->Close();
        m_lastStatus = response.empty() ? "No response received" : "Message exchange successful";
        
        return response;
        
    } catch (const std::exception& e)
    {
        m_lastStatus = "Exception: " + std::string(e.what());
        LogPrint(eLogError, "NormalClient: Exception: ", e.what());
        return "";
    }
}

std::string NormalStreamClient::receiveFromStream(std::shared_ptr<stream::Stream> stream, int timeout_ms)
{
    uint8_t recv_buf[4096];
    std::string data;
    bool end = false;
    int numAttempts = 0;
    const int SHORT_TIMEOUT = 5;  // 5 seconds per attempt
    const int MAX_ATTEMPTS = std::max(1, timeout_ms / (SHORT_TIMEOUT * 1000));
    
    while (!end && numAttempts < MAX_ATTEMPTS)
    {
        LogPrint(eLogDebug, "NormalClient: Receive attempt ", numAttempts + 1, ", stream status=", stream->GetStatus());
        
        if (const size_t received = stream->Receive(recv_buf, 4096, SHORT_TIMEOUT))
        {
            data.append(reinterpret_cast<char*>(recv_buf), received);
            LogPrint(eLogInfo, "NormalClient: Received ", received, " bytes");
            
            // Check if stream is still valid for more data
            if (stream->GetStatus() == stream::eStreamStatusReset ||
                stream->GetStatus() == stream::eStreamStatusClosed)
            {
                end = true;
            }
        } else if (stream->GetStatus() == stream::eStreamStatusReset ||
                  stream->GetStatus() == stream::eStreamStatusClosed)
        {
            LogPrint(eLogInfo, "NormalClient: Stream ended, status=", stream->GetStatus());
            end = true;
        } else
        {
            LogPrint(eLogWarning, "NormalClient: Receive timeout, attempt ", numAttempts + 1);
            numAttempts++;
        }
    }
    
    // Process remaining buffer
    while (const size_t len = stream->ReadSome(recv_buf, sizeof(recv_buf)))
        data.append(reinterpret_cast<char*>(recv_buf), len);
    
    if (!data.empty())
        LogPrint(eLogInfo, "NormalClient: Received response: [", data, "]");
    else
        LogPrint(eLogWarning, "NormalClient: No data received");
    
    return data;
}

bool NormalStreamClient::isReady() const
{
    return m_destination && m_destination->IsReady();
}

std::string NormalStreamClient::getStatus() const
{
    if (!m_destination)
        return "No destination";
    
    std::ostringstream oss;
    oss << "Destination ready: " << (m_destination->IsReady() ? "yes" : "no");
if (!m_lastStatus.empty())
        oss << ", Last operation: " << m_lastStatus;

    return oss.str();
}

// NormalStreamServer Implementation

NormalStreamServer::NormalStreamServer(std::shared_ptr<client::ClientDestination> destination)
    : m_destination(std::move(destination))
{}

NormalStreamServer::~NormalStreamServer()
{
    NormalStreamServer::stop();
}

void NormalStreamServer::start(const MessageHandler handler)
{
    if (m_running.load())
    {
        LogPrint(eLogWarning, "NormalServer: Already running");
        return;
    }
    
    m_handler = handler;
    m_running.store(true);
    
    // Wait for destination to be ready and published
    if (!I2PdUtils::waitForDestinationReady(m_destination, 20000))
    {
        m_lastStatus = "Destination failed to become ready";
        LogPrint(eLogError, "NormalServer: Destination not ready");
        return;
    }
    
    // Extra safety: wait until NetDb can return our LeaseSet
    I2PdUtils::waitForLeaseSet(m_destination->GetIdentHash(), 20000);
    
    LogPrint(eLogInfo, "NormalServer: Server ready, b32: ", getB32Address());
    
    // Accept incoming streams
    m_destination->AcceptStreams([this](std::shared_ptr<stream::Stream> stream) {
        if (m_running.load()) {
            LogPrint(eLogInfo, "NormalServer: Got incoming stream");
            handleIncomingStream(std::move(stream));
        }
    });
    
    m_lastStatus = "Server started and accepting connections";
}

void NormalStreamServer::stop()
{
    if (m_running.load())
    {
        m_running.store(false);
        m_destination->StopAcceptingStreams();
        m_lastStatus = "Server stopped";
        LogPrint(eLogInfo, "NormalServer: Server stopped");
    }
}

std::string NormalStreamServer::getB32Address() const
{
    if (m_destination)
        return m_destination->GetIdentHash().ToBase32() + ".b32.i2p";

    return "";
}

bool NormalStreamServer::isReady() const
{
    return m_destination && m_destination->IsReady() && m_running.load();
}

std::string NormalStreamServer::getStatus() const
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

void NormalStreamServer::handleIncomingStream(std::shared_ptr<stream::Stream> stream)
{
    try {
        receiveLoop(std::move(stream));
    } catch (const std::exception& e) {
        LogPrint(eLogError, "NormalServer: Exception in stream handler: ", e.what());
    }
}

void NormalStreamServer::receiveLoop(std::shared_ptr<stream::Stream> stream)
{
    LogPrint(eLogInfo, "NormalServer: Starting receiveLoop, stream status=", stream->GetStatus());
    
    // Wait for stream establishment
    int establishWait = 0;
    while (!stream->IsEstablished() &&
           stream->GetStatus() != stream::eStreamStatusReset &&
           establishWait < 100)
    {
        std::this_thread::sleep_for(50ms);
        establishWait++;
    }
    
    LogPrint(eLogInfo, "NormalServer: Stream established=", stream->IsEstablished(), ", status=", stream->GetStatus());
    
    // Server-side delay for message propagation
    LogPrint(eLogInfo, "NormalServer: Waiting for client message propagation...");
    std::this_thread::sleep_for(2000ms);
    
    uint8_t recv_buf[4096];
    std::string data;
    bool end = false;
    int numAttempts = 0;
    const int SHORT_TIMEOUT = 5;  // 5 seconds
    const int MAX_ATTEMPTS = 8;   // 8 attempts
    
    while (!end && m_running.load())
    {
        LogPrint(eLogDebug, "NormalServer: Receive attempt ", numAttempts + 1, ", stream status=", stream->GetStatus());
        
        if (const size_t received = stream->Receive(recv_buf, 4096, SHORT_TIMEOUT))
        {
            data.append(reinterpret_cast<char*>(recv_buf), received);
            LogPrint(eLogInfo, "NormalServer: Received ", received, " bytes");
            
            // Check if stream is still valid for more data
            if (stream->GetStatus() == stream::eStreamStatusReset ||
                stream->GetStatus() == stream::eStreamStatusClosed)
            {
                end = true;
            }
        }
        else if (stream->GetStatus() == stream::eStreamStatusReset ||
                 stream->GetStatus() == stream::eStreamStatusClosed ||
                 !m_running.load())
        {
            LogPrint(eLogInfo, "NormalServer: Stream ended or shutting down, status=", stream->GetStatus());
            end = true;
        }
        else
        {
            LogPrint(eLogWarning, "NormalServer: Receive timeout, attempt ", numAttempts + 1);
            numAttempts++;
            if (numAttempts >= MAX_ATTEMPTS)
            {
                LogPrint(eLogError, "NormalServer: Max receive attempts exceeded");
                end = true;
            }
        }
    }
    
    // Process remaining buffer
    while (const size_t len = stream->ReadSome(recv_buf, sizeof(recv_buf)) && m_running.load())
        data.append(reinterpret_cast<char*>(recv_buf), len);
    
    if (!data.empty() && m_running.load())
    {
        LogPrint(eLogInfo, "NormalServer: Got message: [", data, "]");
        
        // Call handler to get response
        std::string response;
        if (m_handler) {
            // Get client identity hash from stream
            auto clientHash = stream->GetRemoteIdentity()->GetIdentHash();
            response = m_handler(data, clientHash);
        } else {
            response = "echo: " + data;  // Default echo
        }
        
        LogPrint(eLogInfo, "NormalServer: Sending response: [", response, "]");
        if (auto sent = stream->Send(reinterpret_cast<const uint8_t*>(response.data()), response.size()); 
            sent != response.size())
        {
            LogPrint(eLogError, "NormalServer: Partial send: ", sent, "/", response.size());
        }
        else
            LogPrint(eLogInfo, "NormalServer: Response sent successfully, ", sent, " bytes");
    }
    else
        LogPrint(eLogWarning, "NormalServer: No data received or shutting down, data.size()=", data.size());
    
    stream->Close();
    LogPrint(eLogInfo, "NormalServer: Stream closed");
}

} // namespace i2p::embed
