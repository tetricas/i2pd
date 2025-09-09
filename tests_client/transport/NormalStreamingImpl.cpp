#include "NormalStreamingImpl.h"
#include "../core/I2PdUtils.h"
#include "../core/FileTransferLogging.h"
#include "../core/TransferConfig.h"
#include "Log.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <utility>

using namespace std::chrono_literals;


namespace i2p::embed {

// NormalStreamClient Implementation

NormalStreamClient::NormalStreamClient(std::shared_ptr<client::ClientDestination> destination)
    : m_destination(std::move(destination)), m_expectingResponse(false) {
    // Single stream approach - no need for stream acceptor
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
        I2PdUtils::waitForLeaseSet(serverHash, i2p::filetransfer::TransferConfig::getLeaseSetTimeout());
        
        FT_LOG_DEBUG("NormalClient", "Using single bidirectional stream for request-response");
        
        // Create bidirectional stream (client ↔ server)
        FT_LOG_DEBUG("NormalClient", "Creating bidirectional stream to server hash: " << serverHash.ToBase32().substr(0,16) << "...");
        auto stream = m_destination->CreateStream(serverHash);
        if (!stream)
        {
            m_lastStatus = "Failed to create stream";
            FT_LOG_ERROR("NormalClient", "CreateStream failed for hash: " << serverHash.ToBase32().substr(0,16) << "...");
            return "";
        }
        
        LogPrint(eLogInfo, "NormalClient: Stream created successfully, RecvStreamID=", stream->GetRecvStreamID(), ", SendStreamID=", stream->GetSendStreamID());
        
        // Send initial handshake immediately to trigger establishment
        LogPrint(eLogInfo, "NormalClient: Sending initial handshake to trigger establishment");
        stream->Send(nullptr, 0);
        
        // Wait for establishment after handshake
        int establishWait = 0;
        LogPrint(eLogInfo, "NormalClient: Waiting for stream establishment after handshake...");
        while (!stream->IsEstablished() && 
               stream->GetStatus() != stream::eStreamStatusReset &&
               establishWait < 100)
        {
            if (establishWait % 10 == 0) {  // Log every 500ms
                LogPrint(eLogInfo, "NormalClient: Stream establishment wait ", establishWait, "/100, status=", stream->GetStatus(), ", established=", stream->IsEstablished());
            }
            std::this_thread::sleep_for(50ms);
            establishWait++;
        }
        
        if (!stream->IsEstablished())
        {
            m_lastStatus = "Stream failed to establish after handshake";
            LogPrint(eLogError, "NormalClient: Stream failed to establish after handshake, final status=", stream->GetStatus());
            return "";
        }
        
        LogPrint(eLogInfo, "NormalClient: Sending request on bidirectional stream: [", message, "]");
        auto sent = stream->Send(reinterpret_cast<const uint8_t*>(message.data()), message.size());
        if (sent != message.size())
        {
            m_lastStatus = "Partial send: " + std::to_string(sent) + "/" + std::to_string(message.size());
            LogPrint(eLogError, "NormalClient: Partial send: ", sent, "/", message.size());
            return "";
        }
        
        LogPrint(eLogInfo, "NormalClient: Request sent successfully: ", sent, " bytes");
        
        // Give server time to process request and send response
        LogPrint(eLogInfo, "NormalClient: Waiting 500ms for server to process and respond...");
        std::this_thread::sleep_for(500ms);
        
        // Receive response on same bidirectional stream
        LogPrint(eLogInfo, "NormalClient: Waiting for response on same stream...");
        std::string response = receiveFromStream(stream, timeout_ms / 2);
        
        stream->Close();
        
        if (!response.empty())
        {
            m_lastStatus = "Message exchange successful";
            return response;
        }
        else
        {
            m_lastStatus = "No response received";
            LogPrint(eLogError, "NormalClient: No response received");
            return "";
        }
        
    } catch (const std::exception& e)
    {
        m_lastStatus = "Exception: " + std::string(e.what());
        LogPrint(eLogError, "NormalClient: Exception: ", e.what());
        return "";
    }
}

std::string NormalStreamClient::receiveFromStream(std::shared_ptr<stream::Stream> stream, int timeout_ms)
{
    LogPrint(eLogInfo, "NormalClient: receiveFromStream starting on RecvStreamID=", stream->GetRecvStreamID(), ", SendStreamID=", stream->GetSendStreamID());
    
    // OPTIMIZATION: Larger buffer for high-throughput streaming
    const size_t BUFFER_SIZE = 1024 * 1024; // 1MB buffer for large file transfers
    std::vector<uint8_t> recv_buf(BUFFER_SIZE);
    std::string data;
    data.reserve(BUFFER_SIZE); // Pre-allocate for efficiency
    
    bool end = false;
    int numAttempts = 0;
    const int SHORT_TIMEOUT = 30;  // 30 seconds per attempt for large transfers
    const int MAX_ATTEMPTS = std::max(1, timeout_ms / (SHORT_TIMEOUT * 1000));
    
    while (!end && numAttempts < MAX_ATTEMPTS)
    {
        LogPrint(eLogDebug, "NormalClient: Receive attempt ", numAttempts + 1, ", stream status=", stream->GetStatus(), ", RecvStreamID=", stream->GetRecvStreamID(), ", SendStreamID=", stream->GetSendStreamID());
        
        if (const size_t received = stream->Receive(recv_buf.data(), BUFFER_SIZE, SHORT_TIMEOUT))
        {
            data.append(reinterpret_cast<char*>(recv_buf.data()), received);
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
            // Only log timeout warnings for small files or first few attempts
            if (numAttempts < 3) {
                LogPrint(eLogWarning, "NormalClient: Receive timeout, attempt ", numAttempts + 1);
            }
            numAttempts++;
        }
    }
    
    // Process remaining buffer
    while (const size_t len = stream->ReadSome(recv_buf.data(), recv_buf.size()))
        data.append(reinterpret_cast<char*>(recv_buf.data()), len);
    
    if (!data.empty()) {
        LogPrint(eLogInfo, "NormalClient: Received response: ", data.size(), " bytes total");
    } else {
        LogPrint(eLogWarning, "NormalClient: No data received");
    }
    
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
    if (!I2PdUtils::waitForDestinationReady(m_destination, i2p::filetransfer::TransferConfig::getConnectionTimeout()))
    {
        m_lastStatus = "Destination failed to become ready";
        LogPrint(eLogError, "NormalServer: Destination not ready");
        return;
    }
    
    // Extra safety: wait until NetDb can return our LeaseSet
    I2PdUtils::waitForLeaseSet(m_destination->GetIdentHash(), i2p::filetransfer::TransferConfig::getLeaseSetTimeout());
    
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
        // Move receive loop to separate thread to avoid blocking streaming thread
        LogPrint(eLogInfo, "NormalServer: Starting separate thread for receiveLoop to avoid blocking streaming");
        std::thread([this, stream = std::move(stream)]() {
            try {
                receiveLoop(stream);
            } catch (const std::exception& e) {
                LogPrint(eLogError, "NormalServer: Exception in receive thread: ", e.what());
            }
        }).detach();
    } catch (const std::exception& e) {
        LogPrint(eLogError, "NormalServer: Exception in stream handler: ", e.what());
    }
}

void NormalStreamServer::receiveLoop(std::shared_ptr<stream::Stream> requestStream)
{
    LogPrint(eLogInfo, "NormalServer: Starting receiveLoop for request stream");
    
    // Wait for stream establishment
    int establishWait = 0;
    while (!requestStream->IsEstablished() &&
           requestStream->GetStatus() != stream::eStreamStatusReset &&
           establishWait < 100)
    {
        std::this_thread::sleep_for(50ms);
        establishWait++;
    }
    
    LogPrint(eLogInfo, "NormalServer: Request stream established=", requestStream->IsEstablished());
    
    // Allow time for data packets to arrive after stream establishment
    LogPrint(eLogInfo, "NormalServer: Waiting 200ms for data packets to arrive...");
    std::this_thread::sleep_for(200ms);
    
    // Receive request on unidirectional stream (client → server)
    LogPrint(eLogInfo, "NormalServer: Starting to receive request data, stream RecvStreamID=", requestStream->GetRecvStreamID(), ", SendStreamID=", requestStream->GetSendStreamID());
    
    std::string request;
    uint8_t recv_buf[4096];
    
    // Use shorter timeout to avoid blocking, but retry multiple times
    LogPrint(eLogInfo, "NormalServer: Calling Receive() with shorter timeouts and retries...");
    
    int attempts = 0;
    size_t totalReceived = 0;
    while (attempts < 6 && totalReceived == 0) {  // 6 attempts = ~30 seconds total
        LogPrint(eLogInfo, "NormalServer: Receive attempt ", attempts + 1, "/6...");
        const size_t received = requestStream->Receive(recv_buf + totalReceived, sizeof(recv_buf) - totalReceived, 5);
        LogPrint(eLogInfo, "NormalServer: Receive() attempt ", attempts + 1, " returned ", received, " bytes");
        
        if (received > 0) {
            totalReceived += received;
            LogPrint(eLogInfo, "NormalServer: Total received so far: ", totalReceived, " bytes");
        }
        attempts++;
        
        // Short sleep between attempts to not overwhelm the stream
        if (received == 0 && attempts < 6) {
            std::this_thread::sleep_for(100ms);
        }
    }
    
    if (totalReceived > 0)
    {
        request.assign(reinterpret_cast<char*>(recv_buf), totalReceived);
        LogPrint(eLogInfo, "NormalServer: Received request: [", request, "], ", totalReceived, " bytes");
    }
    else
    {
        LogPrint(eLogWarning, "NormalServer: No request data received after ", attempts, " attempts, stream status=", requestStream->GetStatus());
    }
    auto clientHash = requestStream->GetRemoteIdentity()->GetIdentHash();
    
    if (!request.empty() && m_running.load())
    {
        LogPrint(eLogInfo, "NormalServer: Got request: [", request, "]");
        
        // Generate response
        std::string response;
        if (m_handler) {
            response = m_handler(request, clientHash);
        } else {
            response = "echo: " + request;
        }
        
        // Send response on same bidirectional stream (no need to create new stream)
        auto sent = requestStream->Send(reinterpret_cast<const uint8_t*>(response.data()), response.size());
        
        if (sent == response.size())
        {
            LogPrint(eLogInfo, "NormalServer: Response sent successfully: ", sent, " bytes");
        }
        else
        {
            LogPrint(eLogError, "NormalServer: Partial response send: ", sent, "/", response.size());
        }
        
        LogPrint(eLogInfo, "NormalServer: Response sent, letting client close the stream");
    }
    else
    {
        LogPrint(eLogWarning, "NormalServer: No request received, data.size()=", request.size());
        LogPrint(eLogInfo, "NormalServer: Closing stream due to no request");
        requestStream->Close();
    }
}

void NormalStreamClient::handleIncomingServerStream(std::shared_ptr<stream::Stream> stream)
{
    LogPrint(eLogInfo, "NormalClient: Received incoming response stream from server");
    
    // Wait for stream establishment
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
        LogPrint(eLogError, "NormalClient: Server response stream failed to establish");
        return;
    }
    
    // Receive response from server on unidirectional stream (server → client)
    std::string response;
    uint8_t recv_buf[4096];
    
    if (const size_t received = stream->Receive(recv_buf, sizeof(recv_buf), 10))
    {
        response.assign(reinterpret_cast<char*>(recv_buf), received);
        LogPrint(eLogInfo, "NormalClient: Received ", received, " bytes");
    }
    else
    {
        LogPrint(eLogWarning, "NormalClient: No response data received");
    }
    
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_receivedResponse = response;
        m_expectingResponse = false;
    }
    
    LogPrint(eLogInfo, "NormalClient: Received response from server: ", response.size(), " bytes");
    m_responseCondition.notify_one();
    
    stream->Close();
    LogPrint(eLogInfo, "NormalClient: Response stream closed");
}

} // namespace i2p::embed
