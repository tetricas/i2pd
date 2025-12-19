#include "StreamInterface.h"
#include "NormalStreamingImpl.h"
#include "SimpleStreamingImpl.h"

namespace i2p::embed {

std::unique_ptr<IStreamClient> StreamFactory::createClient(const ProtocolType type,
                                                          std::shared_ptr<client::ClientDestination> destination) {
    switch (type)
    {
        case ProtocolType::NORMAL_STREAMING:
            return std::make_unique<NormalStreamClient>(destination);
        case ProtocolType::SIMPLE_MESSAGING:
            return std::make_unique<SimpleStreamClient>(destination);
        default:
            throw std::invalid_argument("Unknown protocol type");
    }
}

std::unique_ptr<IStreamServer> StreamFactory::createServer(const ProtocolType type,
                                                          std::shared_ptr<client::ClientDestination> destination) {
    switch (type)
    {
        case ProtocolType::NORMAL_STREAMING:
            return std::make_unique<NormalStreamServer>(destination);
        case ProtocolType::SIMPLE_MESSAGING:
            return std::make_unique<SimpleStreamServer>(destination);
        default:
            throw std::invalid_argument("Unknown protocol type");
    }
}

} // namespace i2p::embed
