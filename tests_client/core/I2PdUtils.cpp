#include "I2PdUtils.h"
#include "FileTransferLogging.h"
#include "TransferConfig.h"
#include "TransferRecovery.h"
#include "StreamStabilityMonitor.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>

#include "Config.h"
#include "FS.h"
#include "Log.h"
#include "NetDb.hpp"
#include "RouterContext.h"
#include "Transports.h"
#include "Tunnel.h"
#include "ClientContext.h"

using namespace std::chrono_literals;

namespace i2p::embed
{

void I2PdUtils::initNode(const std::string& mode, const std::string& datadir, const std::string& config)
{
    config::Init();
    fs::SetAppName("embed-" + mode);
    fs::DetectDataDir(datadir);
    config::ParseConfig(config);
    fs::Init();
    config::Finalize();
    log::Logger().SetLogLevel("info");

    transport::InitAddressFromIface();
    int netID; 
    config::GetOption("netid", netID);
    context.SetNetID(netID);
    bool checkReserved; 
    config::GetOption("reservedrange", checkReserved);
    transport::transports.SetCheckReserved(checkReserved);

    context.Init();
    transport::InitTransports();
    context.SetFloodfill(false);
    bool transit; 
    config::GetOption("notransit", transit);
    context.SetAcceptsTunnels(!transit);
    std::string bandwidth; 
    config::GetOption("bandwidth", bandwidth);
    context.SetBandwidth(bandwidth[0]);
    FT_LOG_INFO("I2PdUtils", "Bandwidth set to " << context.GetBandwidthLimit() << "KBps");
    initTrust();
}

void I2PdUtils::startCore()
{
    log::Logger().Start();
    FT_LOG_INFO("I2PdUtils", "Starting NetDB");
    data::netdb.Start();
    transport::transports.Start(true, false); // NTCP2 on, SSU2 off
    if (transport::transports.IsBoundNTCP2()) {
        FT_LOG_INFO("I2PdUtils", "Transports started");
        verifyNTCP2Published();
    }
    FT_LOG_INFO("I2PdUtils", "Starting Tunnels");
    tunnel::tunnels.Start();
    FT_LOG_INFO("I2PdUtils", "Starting Router context");
    context.Start();
}

void I2PdUtils::stopCore()
{
    context.Stop();
    tunnel::tunnels.Stop();
    transport::transports.Stop();
    log::Logger().Stop();
}

std::shared_ptr<client::ClientDestination> I2PdUtils::createDestination(const bool isPublic, const int hops)
{
    const auto keys = data::PrivateKeys::CreateRandomKeys(data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
    
    // Default tunnel parameters
    auto params = std::map<std::string, std::string>{
                {"inbound.length", std::to_string(hops)},
                {"outbound.length", std::to_string(hops)},
                {"inbound.quantity", std::to_string(hops+1)},
                {"outbound.quantity", std::to_string(hops+1)},
                {"i2cp.leaseSetEncType", "0"}
    };
    params["i2cp.dontPublishLeaseSet"] = isPublic ? "false" : "true";
    
    auto destination = client::context.CreateNewLocalDestination(keys, isPublic, &params);
    destination->Start();
    
    // Log configured hop count for verification
    const auto inboundLength = params.find("inbound.length");
    const auto outboundLength = params.find("outbound.length");
    if (inboundLength != params.end() && outboundLength != params.end()) {
        FT_LOG_INFO("I2PdUtils", "Created destination with tunnel configuration: inbound=" << inboundLength->second << " hops, outbound=" << outboundLength->second << " hops");
    }
    
    return destination;
}

void I2PdUtils::verifyNTCP2Published()
{
    const auto& ri = context.GetRouterInfo();
    const auto addrs = ri.GetAddresses();
    if (!addrs)
        throw std::runtime_error("RouterInfo addresses not ready yet");

    bool ok = false;
    for (const auto& a : *addrs)
    {
        if (a && a->transportStyle == data::RouterInfo::eTransportNTCP2)
        {
            std::cout << "NTCP2 in RI: " << a->host.to_string() << ":" << a->port << "\n";
            ok = true;
        }
    }
    if (!ok)
        throw std::runtime_error("No NTCP2 address in RI (check [ntcp2] + reservedrange=false)");
}

void I2PdUtils::initTrust()
{
    bool trust; 
    config::GetOption("trust.enabled", trust);
    if (trust)
    {
        FT_LOG_INFO("I2PdUtils", "Explicit trust enabled");
        std::string fam; 
        config::GetOption("trust.family", fam);
        std::string routers; 
        config::GetOption("trust.routers", routers);
        bool restricted = false;
        
        if (!fam.empty())
        {
            std::set<std::string> fams;
            size_t pos = 0, comma;
            do {
                comma = fam.find(',', pos);
                fams.insert(fam.substr(pos, comma != std::string::npos ? comma - pos : std::string::npos));
                pos = comma + 1;
            } while (comma != std::string::npos);
            transport::transports.RestrictRoutesToFamilies(fams);
            restricted = !fams.empty();
        }
        
        if (!routers.empty())
        {
            std::set<data::IdentHash> idents;
            size_t pos = 0, comma;
            do {
                comma = routers.find(',', pos);
                data::IdentHash ident;
                ident.FromBase64(routers.substr(pos, comma != std::string::npos ? comma - pos : std::string::npos));
                idents.insert(ident);
                pos = comma + 1;
            } while (comma != std::string::npos);
            FT_LOG_INFO("I2PdUtils", "Setting restricted routes to use " << idents.size() << " trusted routers");
            transport::transports.RestrictRoutesToRouters(idents);
            restricted = !idents.empty();
        }
        
        if (!restricted)
            FT_LOG_ERROR("I2PdUtils", "No trusted routers of families specified");
    }

    bool hidden; 
    config::GetOption("trust.hidden", hidden);
    if (hidden)
    {
        FT_LOG_INFO("I2PdUtils", "Hidden mode enabled");
        context.SetHidden(true);
    }
}

data::IdentHash I2PdUtils::parseBase32(const std::string& base32)
{
    data::IdentHash serverHash;
    
    // Extract just the base32 part if full .b32.i2p address is provided
    std::string cleanBase32 = base32;
    size_t pos = base32.find(".b32.i2p");
    if (pos != std::string::npos) {
        cleanBase32 = base32.substr(0, pos);
    }
    
    if (!serverHash.FromBase32(cleanBase32))
        throw std::runtime_error("Invalid base32 address: " + base32);
    return serverHash;
}

bool I2PdUtils::waitForDestinationReady(std::shared_ptr<client::ClientDestination> destination, const int timeout_ms)
{
    const int check_interval_ms = i2p::filetransfer::TransferConfig::getRetryDelayMs();
    const int max_checks = timeout_ms / check_interval_ms;
    
    for (int i = 0; i < max_checks && !destination->IsReady(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
    }
    
    return destination->IsReady();
}

void I2PdUtils::waitForLeaseSet(const data::IdentHash& identHash, const int timeout_ms)
{
    const int check_interval_ms = i2p::filetransfer::TransferConfig::getRetryDelayMs();
    const int max_checks = timeout_ms / check_interval_ms;
    
    for (int i = 0; i < max_checks && !data::netdb.FindLeaseSet(identHash); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
    }
}

void I2PdUtils::configureExploratoryTunnels(int hopCount)
{
    // Update the config singleton to set exploratory tunnel lengths and quantities
    config::SetOption("exploratory.inbound.length", std::to_string(hopCount));
    config::SetOption("exploratory.outbound.length", std::to_string(hopCount));
    config::SetOption("exploratory.inbound.quantity", std::to_string(hopCount + 1));
    config::SetOption("exploratory.outbound.quantity", std::to_string(hopCount + 1));
    
    FT_LOG_INFO("I2PdUtils", "Configured exploratory tunnels: " << hopCount << " hops (quantity: " << (hopCount + 1) << ")");
}

void I2PdUtils::shutdownRecoverySystems()
{
    FT_LOG_INFO("I2PdUtils", "Shutting down recovery systems...");
    
    // Shutdown recovery systems in proper order to prevent static destruction issues
    i2p::core::TransferRecovery::getInstance().shutdown();
    i2p::core::StreamStabilityMonitor::getInstance().shutdown();
    
    FT_LOG_INFO("I2PdUtils", "Recovery systems shutdown complete");
}

} // namespace i2p::embed