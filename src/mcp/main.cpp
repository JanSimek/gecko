#include "mcp/McpServer.h"
#include "resource/GameResources.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {
void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " --data <dir-or-.dat> [--data <...>]\n"
              << "  A Model Context Protocol (MCP) server over stdio for inspecting and generating\n"
              << "  Fallout 2 maps. Mount the game data with --data, then speak newline-delimited\n"
              << "  JSON-RPC on stdin. Tools: list_maps, analyze, proto_info, generate.\n";
}
} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    std::vector<std::string> dataPaths;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--data" && i + 1 < args.size()) {
            dataPaths.push_back(args[++i]);
        } else if (args[i] == "--help" || args[i] == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "error: unexpected argument: " << args[i] << "\n";
            printUsage(argv[0]);
            return 2;
        }
    }

    // stdout carries JSON-RPC only; silence the resource/reader logging (it goes to stderr anyway).
    spdlog::set_level(spdlog::level::off);

    geck::resource::GameResources resources;
    for (const auto& path : dataPaths) {
        try {
            resources.files().addDataPath(path);
        } catch (const std::exception& e) {
            std::cerr << "warning: could not mount data path '" << path << "': " << e.what() << "\n";
        }
    }

    geck::mcp::McpServer server(resources);

    // MCP stdio transport: one JSON-RPC message per line, one response line per request.
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        nlohmann::json request;
        try {
            request = nlohmann::json::parse(line);
        } catch (const std::exception& e) {
            const nlohmann::json err{ { "jsonrpc", "2.0" }, { "id", nullptr },
                { "error", { { "code", -32700 }, { "message", std::string("parse error: ") + e.what() } } } };
            std::cout << err.dump() << "\n"
                      << std::flush;
            continue;
        }
        const nlohmann::json response = server.handleMessage(request);
        if (!response.is_null()) {
            std::cout << response.dump() << "\n"
                      << std::flush;
        }
    }
    return 0;
}
