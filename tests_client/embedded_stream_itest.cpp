#include <iostream>
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

#include "ClientContext.h"   // i2p::client::context
#include "Streaming.h"       // i2p::stream::Stream
#include "Identity.h"        // i2p::data::IdentHash
#include "FS.h"

using namespace std::chrono_literals;

static std::atomic<bool> g_running{true};
constexpr int RECV_TIMEOUT_MS = 30000; // 30s, tweak as you like

// --- Helper: print our address (b32) ---
static std::string ToB32(const i2p::data::IdentHash& ih) {
    return ih.ToBase32(); // this exists on i2pd; if not, use ih.ToBase32String()
}

int main(int argc, char** argv)
{
    // Modes: server | client <server_b32>
    if (argc < 2) {
        std::cerr << "Usage:\n  " << argv[0] << " server\n  " << argv[0] << " client <server_b32>\n";
        return 2;
    }

    std::string datadir;
    if (const char* env = std::getenv("I2PD_DATA_DIR")) {
        datadir = env;
    } else {
        datadir = (std::filesystem::temp_directory_path() / "i2pd-itest").string();
    }
    std::error_code ec;
    std::filesystem::create_directories(datadir, ec); // ignore if exists

    // Tell i2pd where to write stuff
    i2p::fs::SetAppName("i2pd-itest");
    i2p::fs::DetectDataDir(datadir);
    i2p::fs::Init();

    // 1) Start embedded client/router
    i2p::client::context.Start(); // NOTE: if your branch needs options, set them before Start()

    // Give the router a moment to bootstrap minimal services
    std::this_thread::sleep_for(500ms);

    // 2) Create local destinations for server/client
    //    CreateNewLocalDestination(bool isPublic, SigningKeyType)
    //    Ed25519 signing key is recommended.
    std::shared_ptr<i2p::client::ClientDestination> serverDest;
    std::shared_ptr<i2p::client::ClientDestination> clientDest;

    const bool isServer = std::string(argv[1]) == "server";
    const std::string serverAddr = isServer ? "" : (argc >= 3 ? argv[2] : "");
    if (isServer) {
        serverDest = i2p::client::context.CreateNewLocalDestination(
            /*isPublic*/ true,
            i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519
        );
        // Some branches require explicit Start(); uncomment if needed:
        // serverDest->Start();

        // Wait until our LeaseSet is ready (so peers can connect)
        while (!serverDest->IsReady()) std::this_thread::sleep_for(100ms);

        // Get ident hash / printable address:
        const auto ih = serverDest->GetIdentHash();
        std::cout << "SERVER b32: " << ToB32(ih) << "\n";

        // 3) Accept inbound stream(s) and echo what we read
        serverDest->AcceptStreams([&](std::shared_ptr<i2p::stream::Stream> s) {
            std::uint8_t buf[64 * 1024];
            // was: int n = s->Receive(buf, sizeof(buf));
            size_t n = s->Receive(buf, sizeof(buf), RECV_TIMEOUT_MS);
            if (n > 0) {
                std::string got(reinterpret_cast<char*>(buf), n);
                std::cout << "SERVER got: [" << got << "]\n";
                const std::string reply = "echo: " + got;
                // Send likely returns size_t; using auto avoids type mismatch
                auto sent = s->Send(reinterpret_cast<const uint8_t*>(reply.data()), reply.size());
                if (sent != reply.size()) std::cerr << "SERVER partial send: " << sent << "\n";
            }
            s->Close();
        });

        // Keep process alive to accept streams (Ctrl+C to exit)
        while (true) std::this_thread::sleep_for(1s);

    } else {
        if (serverAddr.empty()) {
            std::cerr << "client requires <server_b32>\n";
            i2p::client::context.Stop();
            return 2;
        }

        clientDest = i2p::client::context.CreateNewLocalDestination(
            /*isPublic*/ false,
            i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519
        );
        // clientDest->Start(); // uncomment if your branch requires it

        while (!clientDest->IsReady()) std::this_thread::sleep_for(100ms);

        // 4) Resolve serverAddr (b32 -> IdentHash)
        i2p::data::IdentHash remoteIH;
        // Most branches have FromBase32(string); if not, use i2p::data::Base32ToByteStream(...)
        remoteIH.FromBase32(serverAddr);

        // 5) Connect and send
        auto stream = clientDest->CreateStream(remoteIH); // or CreateStream(remote LeaseSet)
        if (!stream) {
            std::cerr << "CreateStream failed\n";
            i2p::client::context.Stop();
            return 1;
        }

        const std::string msg = "hello over embedded i2pd\n";
        bool ok = stream->Send(reinterpret_cast<const uint8_t*>(msg.data()), msg.size()); // or Write(...)
        if (!ok) {
            std::cerr << "Send failed\n";
            stream->Close();
            i2p::client::context.Stop();
            return 1;
        }

        std::uint8_t buf[64 * 1024];
        size_t n = stream->Receive(buf, sizeof(buf), RECV_TIMEOUT_MS);
        std::cout << "CLIENT got: [" << std::string(reinterpret_cast<char*>(buf), n) << "]\n";
        stream->Close();

        // 6) Cleanup
        i2p::client::context.Stop();
    }

    return 0;
}