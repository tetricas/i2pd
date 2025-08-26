#include "SimpleStreamingImpl.h"
#include "I2PdUtils.h"
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
            LogPrint(eLogError, "SimpleClient: CreateStream failed");
            return "";
        }
        
        LogPrint(eLogInfo, "SimpleClient: Stream created, status: ", stream->GetStatus());
        
        // Wait for stream to establish connection - but SimpleSend works even without establishment
        for (int i = 0; i < 50 && !stream->IsEstablished(); ++i)
        {
            std::this_thread::sleep_for(100ms);
        }
        
        LogPrint(eLogInfo, "SimpleClient: Stream established: ", stream->IsEstablished());
        LogPrint(eLogInfo, "SimpleClient: Stream status: ", stream->GetStatus());
        LogPrint(eLogInfo, "SimpleClient: Sending message via SendEcho: [", message, "]");
        
        // Use SendEcho for reliable request-response (this uses ping protocol)
        stream->SimpleSend(message, true); // true = expect response
        
        LogPrint(eLogInfo, "SimpleClient: SendEcho sent, waiting for response...");
        
        // Use ReceiveEcho to get the response
        std::string response = stream->SimpleReceive(timeout_ms / 2);
        
        if (!response.empty())
        {
            LogPrint(eLogInfo, "SimpleClient: ReceiveEcho success, got: [", response, "]");
            m_lastStatus = "Message exchange successful";
        }
        else
        {
            m_lastStatus = "ReceiveEcho timeout or failed";
            LogPrint(eLogWarning, "SimpleClient: ReceiveEcho timeout or failed");
        }
        
        return response;
        
    }
    catch (const std::exception& e)
    {
        m_lastStatus = "Exception: " + std::string(e.what());
        LogPrint(eLogError, "SimpleClient: Exception: ", e.what());
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
        LogPrint(eLogWarning, "SimpleServer: Already running");
        return;
    }
    
    m_handler = handler;
    m_running.store(true);
    
    // Wait for destination to be ready and published
    if (!I2PdUtils::waitForDestinationReady(m_destination, 20000))
    {
        m_lastStatus = "Destination failed to become ready";
        LogPrint(eLogError, "SimpleServer: Destination not ready");
        return;
    }
    
    // Extra safety: wait until NetDb can return our LeaseSet
    I2PdUtils::waitForLeaseSet(m_destination->GetIdentHash(), 20000);
    
    LogPrint(eLogInfo, "SimpleServer: Server ready, b32: ", getB32Address());
    
    // Accept streams to handle incoming ping-based echo requests
    m_destination->AcceptStreams([this](std::shared_ptr<stream::Stream> stream) {
        if (m_running.load())
        {
            LogPrint(eLogInfo, "SimpleServer: Got incoming stream for echo handling");
            
            // Keep the stream alive and monitor for custom handling
            std::thread([this, stream = std::move(stream)]() {
                try {
                    while (m_running.load() && stream->GetStatus() != stream::eStreamStatusClosed) {
                        // The stream's HandlePing automatically processes echo requests
                        // but we can also monitor SimpleReceive for custom handling
                        std::this_thread::sleep_for(1000ms);
                    }
                    LogPrint(eLogInfo, "SimpleServer: Stream closed");
                } catch (const std::exception& e) {
                    LogPrint(eLogError, "SimpleServer: Stream thread exception: ", e.what());
                }
            }).detach();
        }
    });

    m_lastStatus = "Server started and accepting connections";
}

void SimpleStreamServer::stop()
{
    if (m_running.load())
    {
        m_running.store(false);
        m_destination->StopAcceptingStreams();
        m_lastStatus = "Server stopped";
        LogPrint(eLogInfo, "SimpleServer: Server stopped");
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

void SimpleStreamServer::handleIncomingStream(std::shared_ptr<stream::Stream> stream)
{
    try {
        LogPrint(eLogInfo, "SimpleServer: Handling stream, status=", stream->GetStatus());
        
        // Use SimpleReceive to wait for client message
        LogPrint(eLogInfo, "SimpleServer: Waiting for simple message...");

        if (auto received = stream->SimpleReceive(15000); !received.empty() && m_running.load())
        {
            LogPrint(eLogInfo, "SimpleServer: SimpleReceive success, got: [", received, "]");
            
            // Call handler to get response
            std::string response;
            if (m_handler)
                response = m_handler(received);
            else
                response = "echo: " + received;  // Default echo
            
            LogPrint(eLogInfo, "SimpleServer: Sending response: [", response, "]");
            
            // Send response back to client using SimpleSend
            stream->SimpleSend(response);
        }
        else
            LogPrint(eLogWarning, "SimpleServer: SimpleReceive timeout or failed, received=", received);
        
        // Brief wait before cleanup
        std::this_thread::sleep_for(1000ms);
        
    }
    catch (const std::exception& e)
    {
        LogPrint(eLogError, "SimpleServer: Exception in stream handler: ", e.what());
    }
}

} // namespace i2p::embed
