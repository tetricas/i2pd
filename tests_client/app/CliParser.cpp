#include "CliParser.h"
#include <stdexcept>
#include <iostream>

namespace i2p {
namespace embed {

CliArgs CliParser::parse(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error("Missing required arguments. Use --help for usage information.");
    }
    
    CliArgs args;
    args.mode = argv[1];
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        
        auto next = [&](std::string& dst) {
            if (++i >= argc) {
                throw std::runtime_error("Missing value for " + arg);
            }
            dst = argv[i];
        };
        
        if (arg == "--datadir") {
            next(args.datadir);
        } else if (arg == "--conf") {
            next(args.conf);
        } else if (arg == "--server-b32") {
            next(args.server_b32);
        } else if (arg == "--message") {
            next(args.message);
        } else if (arg == "--simple") {
            args.useSimpleProtocol = true;
        } else if (arg == "--normal") {
            args.useSimpleProtocol = false;
        } else if (arg == "--help") {
            printUsage();
            exit(0);
        } else {
            throw std::runtime_error("Unknown option: " + arg);
        }
    }
    
    // Validate required arguments
    if (args.datadir.empty()) {
        throw std::runtime_error("--datadir is required");
    }
    
    if (args.mode == "client" && args.server_b32.empty()) {
        throw std::runtime_error("client mode requires --server-b32");
    }
    
    if (args.mode != "server" && args.mode != "client") {
        throw std::runtime_error("mode must be 'server' or 'client'");
    }
    
    return args;
}

void CliParser::printUsage() {
    std::cout << R"(
Usage:
  server --datadir DIR [--conf FILE] [--simple|--normal]
  client --datadir DIR --server-b32 <b32> [--conf FILE] [--message TEXT] [--simple|--normal]

Options:
  --datadir DIR     Data directory for i2pd
  --conf FILE       Configuration file path
  --server-b32 B32  Server's base32 address (client mode only)
  --message TEXT    Message to send (client mode only, default: "hello")
  --simple          Use SimpleSend/SimpleReceive protocol (default, reliable)
  --normal          Use standard i2pd streaming protocol (requires stable network)
  --help            Show this help message

Protocol Options:
  --simple:  Uses SimpleSend/SimpleReceive - works reliably in all network configurations
  --normal:  Uses standard streaming - requires multi-hop tunnels and stable routing

Examples:
  # Start server with SimpleSend/SimpleReceive (recommended)
  ./embedded_stream_itest server --datadir /tmp/server --simple

  # Start client with custom message
  ./embedded_stream_itest client --datadir /tmp/client --server-b32 abc123...xyz --message "test message"

  # Use normal streaming (only if you have proper i2pd network setup)
  ./embedded_stream_itest server --datadir /tmp/server --normal
)";
}

} // namespace embed
} // namespace i2p