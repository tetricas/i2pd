#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "ClientContext.h"
#include "Config.h"
#include "FS.h"
#include "Log.h"
#include "NetDb.hpp"
#include "RouterContext.h"
#include "Transports.h"
#include "Tunnel.h"

using namespace std::chrono_literals;

namespace {
constexpr int RECV_TIMEOUT_S = 10;

std::atomic g_running{true};
void on_sigint(int){ g_running = false; }

struct Cli {
    std::string mode;      // server | client
    bool pingOnly{false};  // --pingOnly
    std::string datadir;   // --datadir DIR
    std::string conf;      // --conf FILE
    std::string server_b32;// (client) --server-b32 <b32>
};

Cli parseCli(int argc, char** argv)
{
    if (argc < 2)
        throw std::runtime_error("Usage:\n"
                                 "  server --datadir DIR [--pingOnly] [--conf FILE]\n"
                                 "  client --datadir DIR [--pingOnly] [--conf FILE] --server-b32 <b32>\n");
    Cli c; c.mode = argv[1];
    for (int i=2;i<argc;++i)
    {
        std::string k = argv[i];
        auto next = [&](std::string& dst) {
            if (++i>=argc)
                throw std::runtime_error("Missing value for "+k);
            dst = argv[i];
        };

        if (k=="--datadir")
            next(c.datadir);
        else if (k=="--conf")
            next(c.conf);
        else if (k=="--server-b32")
            next(c.server_b32);
        else if (k=="--pingOnly")
            c.pingOnly = true;
        else
            throw std::runtime_error("Unknown option: "+k);
    }
    if (c.datadir.empty())
        throw std::runtime_error("--datadir is required");
    if (c.mode=="client" && c.server_b32.empty())
        throw std::runtime_error("client requires --server-b32");
    if (c.mode!="server" && c.mode!="client")
        throw std::runtime_error("mode must be 'server' or 'client'");

    return c;
}

void verifyNTCP2Published()
{
    const auto& ri = i2p::context.GetRouterInfo();
    const auto addrs = ri.GetAddresses();
    if (!addrs)
        throw std::runtime_error("RouterInfo addresses not ready yet");
    bool ok=false;
    for (const auto& a : *addrs)
    {
        if (a && a->transportStyle == i2p::data::RouterInfo::eTransportNTCP2)
        {
            std::cout << "NTCP2 in RI: " << a->host.to_string() << ":" << a->port << "\n";
            ok=true;
        }
    }
    if (!ok)
        throw std::runtime_error("No NTCP2 address in RI (check [ntcp2] + reservedrange=false)");
}

void initTrust()
{
    bool trust; i2p::config::GetOption("trust.enabled", trust);
    if (trust)
    {
        LogPrint(eLogInfo, "Explicit trust enabled");
        std::string fam; i2p::config::GetOption("trust.family", fam);
        std::string routers; i2p::config::GetOption("trust.routers", routers);
        bool restricted = false;
        if (!fam.empty())
        {
            std::set<std::string> fams;
            size_t pos = 0, comma;
            do
            {
                comma = fam.find (',', pos);
                fams.insert (fam.substr (pos, comma != std::string::npos ? comma - pos : std::string::npos));
                pos = comma + 1;
            }
            while (comma != std::string::npos);
            i2p::transport::transports.RestrictRoutesToFamilies(fams);
            restricted = fams.size() > 0;
        }
        if (!routers.empty()) {
            std::set<i2p::data::IdentHash> idents;
            size_t pos = 0, comma;
            do
            {
                comma = routers.find (',', pos);
                i2p::data::IdentHash ident;
                ident.FromBase64 (routers.substr (pos, comma != std::string::npos ? comma - pos : std::string::npos));
                idents.insert (ident);
                pos = comma + 1;
            }
            while (comma != std::string::npos);
            LogPrint(eLogInfo, "Setting restricted routes to use ", idents.size(), " trusted routers");
            i2p::transport::transports.RestrictRoutesToRouters(idents);
            restricted = !idents.empty();
        }
        if(!restricted)
            LogPrint(eLogError, "No trusted routers of families specified");
    }

    bool hidden; i2p::config::GetOption("trust.hidden", hidden);
    if (hidden)
    {
        LogPrint(eLogInfo, "Daemon: Hidden mode enabled");
        i2p::context.SetHidden(true);
    }
}

void initNode(const std::string& mode, const std::string& datadir, const std::string& config)
{
    i2p::fs::SetAppName("embed-" + mode);
    i2p::fs::DetectDataDir(datadir);
    i2p::config::ParseConfig(config);
    i2p::fs::Init();
    i2p::config::Finalize();
    i2p::log::Logger().SetLogLevel("info");

    i2p::transport::InitAddressFromIface ();
    int netID; i2p::config::GetOption("netid", netID);
    i2p::context.SetNetID (netID);
    bool checkReserved; i2p::config::GetOption("reservedrange", checkReserved);
    i2p::transport::transports.SetCheckReserved(checkReserved);

    i2p::context.Init();
    i2p::transport::InitTransports();
    i2p::context.SetFloodfill (false);
    bool transit; i2p::config::GetOption("notransit", transit);
    i2p::context.SetAcceptsTunnels (!transit);
    std::string bandwidth; i2p::config::GetOption("bandwidth", bandwidth);
    i2p::context.SetBandwidth (bandwidth[0]);
    LogPrint(eLogInfo, "Bandwidth set to ", i2p::context.GetBandwidthLimit (), "KBps");
    initTrust();
}

void start_core()
{
    i2p::log::Logger().Start();
    LogPrint(eLogInfo, "Starting NetDB");
    i2p::data::netdb.Start();
    i2p::transport::transports.Start(true, false); // NTCP2 on, SSU2 off
    if (i2p::transport::transports.IsBoundNTCP2())
    {
        LogPrint(eLogInfo, "Transports started");
        verifyNTCP2Published();
    }
    LogPrint(eLogInfo, "Starting Tunnels");
    i2p::tunnel::tunnels.Start();

    LogPrint(eLogInfo, "Starting Router context");
    i2p::context.Start();
}

void stop_core()
{
    i2p::context.Stop();
    i2p::tunnel::tunnels.Stop();
    i2p::transport::transports.Stop();
    i2p::log::Logger().Stop();
}

void recvLoop(std::shared_ptr<i2p::stream::Stream> stream, const std::string& mode)
{
    uint8_t recv_buf[4096];
    std::string data;
    bool end = false;
    int numAttempts = 0;
    while (!end)
    {
        if (const size_t received = stream->Receive (recv_buf, 4096, RECV_TIMEOUT_S * 2))
        {
            data.append (reinterpret_cast<char *>(recv_buf), received);
            if (!stream->IsOpen ())
                end = true;
        }
        else if (!stream->IsOpen () || !g_running)
            end = true;
        else
        {
            LogPrint (eLogError, "Client-server: request timeout expired");
            numAttempts++;
            if (numAttempts > 5)
                end = true;
        }
    }
    // process remaining buffer
    while (const size_t len = stream->ReadSome (recv_buf, sizeof(recv_buf)) && g_running)
        data.append (reinterpret_cast<char *>(recv_buf), len);

    if (!data.empty() && !g_running)
    {
        LogPrint(eLogNone, mode, " got: [", data, "]");

        if (mode == "Server")
        {
            const std::string reply = "echo: " + data;
            if (auto sent = stream->Send(reinterpret_cast<const uint8_t*>(reply.data()), reply.size()); sent != reply.size())
                LogPrint(eLogNone, mode, ": partial send: ", sent);
        }
    }
    else
        LogPrint(eLogWarning, mode, ": no payload before deadline; closing");

    stream->Close();
    LogPrint(eLogInfo, mode, " closed: stream");
}

std::map<std::string,std::string> kZeroHop{
    {"inbound.length","0"}, {"outbound.length","0"},
    {"inbound.lengthVariance","0"}, {"outbound.lengthVariance","0"},
    {"inbound.quantity","1"}, {"outbound.quantity","1"},
    {"i2cp.leaseSetEncType","0"}
};

} // namespace

int main(int argc, char** argv) try {
    std::signal(SIGINT, on_sigint);
    std::signal(SIGTERM, on_sigint);

    i2p::config::Init();
    auto [mode, pingOnly, datadir, conf, server_b32] = parseCli(argc, argv);

    initNode(mode, datadir, conf);
    start_core();

    if (mode == "server")
    {
        auto keys = i2p::data::PrivateKeys::CreateRandomKeys(i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
        kZeroHop["i2cp.dontPublishLeaseSet"] = "false";
        auto server = i2p::client::context.CreateNewLocalDestination(keys, /*isPublic*/ true, &kZeroHop);
        server->Start();

        // 1) Wait until destination built AND LS published to FF
        for (int i = 0; i < 200 && !server->IsReady(); ++i)
            std::this_thread::sleep_for(100ms);

        // Extra safety: wait until NetDb can actually return our LS
        for (int i = 0; i < 200 && !i2p::data::netdb.FindLeaseSet(server->GetIdentHash()); ++i)
            std::this_thread::sleep_for(100ms);

        // Now it’s safe to announce
        std::cout << "Server b32: " << server->GetIdentHash().ToBase32() << ".b32.i2p\n";

        server->AcceptStreams([pingOnly](std::shared_ptr<i2p::stream::Stream> s){
            LogPrint(eLogInfo, "Server got: stream");
            if (pingOnly)
            {
                s->SendPing();
                std::this_thread::sleep_for(3min); // populate logs
                g_running.store(false);
            }
            else
                recvLoop(std::move(s), "Server");
        });

        while (g_running)
            std::this_thread::sleep_for(250ms);
    }
    else
    {
        // CLIENT
        auto keys = i2p::data::PrivateKeys::CreateRandomKeys(i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
        kZeroHop["i2cp.dontPublishLeaseSet"] = "true";
        auto dest = i2p::client::context.CreateNewLocalDestination(keys, /*isPublic*/ false, &kZeroHop);
        dest->Start();
        for (int i=0;i<60 && !dest->IsReady();++i)
            std::this_thread::sleep_for(100ms);

        // Resolve and connect via NetDb (FF handles lookups in your private net)
        i2p::data::IdentHash serverHash;
        if (!serverHash.FromBase32(server_b32))
          throw std::runtime_error("Invalid --server-b32");

        // Optional prefetch
        dest->RequestDestination(serverHash);
        for (int i = 0; i < 200 && !dest->FindLeaseSet (serverHash); ++i)
            std::this_thread::sleep_for(100ms);

        auto stream = dest->CreateStream(serverHash);
        for (int i=0;i<50 && !stream; ++i)
        {
            std::this_thread::sleep_for(100ms);
            stream = dest->CreateStream(serverHash);
        }
        if (!stream)
            throw std::runtime_error("CreateStream failed");

        LogPrint(eLogNone, "Stream status: ", stream->GetStatus());

        LogPrint(eLogNone, "Client send: empty to initiate connect");
        stream->Send (nullptr, 0); //connect

        while (!stream->IsEstablished())
            std::this_thread::sleep_for(100ms);

        if (pingOnly)
        {
            stream->SendPing("hello");
            std::this_thread::sleep_for(3min); // populate logs
            return 0;
        }

        LogPrint(eLogNone, "Stream status: ", stream->GetStatus());

        const std::string msg = "hello";
        LogPrint(eLogNone, "Client send: ", msg);
        if (auto sent = stream->Send(reinterpret_cast<const uint8_t*>(msg.data()), msg.size()); sent != msg.size())
            throw std::runtime_error("Client partial/failed send");

        LogPrint(eLogNone, "Stream status: ", stream->GetStatus());
        if (!stream->IsOpen())
            throw std::runtime_error("Stream is not open");

        recvLoop(stream, "Client");
    }

    stop_core();
    return 0;
}
catch (const char* msg)
{
    std::cerr << "FATAL: " << msg << "\n";
    stop_core();
    return 1;
}
catch (const std::exception& e)
{
    std::cerr << "FATAL: " << e.what() << "\n";
    try
    {
        stop_core();
    }
    catch(...) {}

    return 1;
}
