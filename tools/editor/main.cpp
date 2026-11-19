#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "insys/editor/editor_server.h"

namespace {

#ifdef INSYS_EDITOR_WEB_DIST
constexpr const char* kDefaultWebDist = INSYS_EDITOR_WEB_DIST;
#else
constexpr const char* kDefaultWebDist = "";
#endif

insys::editor::EditorServer* g_server = nullptr;

void handle_signal(int) {
    if (g_server != nullptr)
        g_server->stop();
}

bool parse_port(const std::string& text, std::uint16_t& port) {
    try {
        std::size_t consumed = 0;
        const unsigned long value = std::stoul(text, &consumed);
        if (consumed != text.size() || value > 65535)
            return false;
        port = static_cast<std::uint16_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

std::filesystem::path default_static_root() {
    if (kDefaultWebDist[0] != '\0') {
        const std::filesystem::path configured(kDefaultWebDist);
        if (std::filesystem::is_directory(configured))
            return configured;
    }
    const std::filesystem::path local = "editor/web/dist";
    if (std::filesystem::is_directory(local))
        return local;
    return {};
}

void print_usage() {
    std::cerr << "usage: insys_editor_server [options]\n"
                 "  --port N             listen port (default 17800)\n"
                 "  --host ADDRESS       bind address (default 127.0.0.1)\n"
                 "  --static PATH        web frontend dist directory\n"
                 "  --asset-root PATH    allowed asset root (repeatable)\n"
                 "  --allow-origin URL   additional allowed WebSocket origin\n"
                 "  --token TOKEN        fixed token (development only)\n";
}

} // namespace

int main(int argc, char** argv) {
    insys::editor::EditorServerOptions options;
    options.port = 17800;
    options.serverVersion = "0.1.0";
    options.session.assetRoots.push_back(std::filesystem::current_path());

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto require_value = [&](const char* name, std::string& value) {
            if (i + 1 >= argc) {
                std::cerr << "insys_editor_server: missing value for " << name << "\n";
                return false;
            }
            value = argv[++i];
            return true;
        };

        if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        }
        if (argument == "--port") {
            std::string value;
            if (!require_value("--port", value) || !parse_port(value, options.port)) {
                std::cerr << "insys_editor_server: invalid port\n";
                return 2;
            }
        } else if (argument == "--host") {
            if (!require_value("--host", options.host))
                return 2;
        } else if (argument == "--static") {
            std::string value;
            if (!require_value("--static", value))
                return 2;
            options.staticRoot = value;
        } else if (argument == "--asset-root") {
            std::string value;
            if (!require_value("--asset-root", value))
                return 2;
            if (options.session.assetRoots.size() == 1 &&
                options.session.assetRoots.front() == std::filesystem::current_path()) {
                options.session.assetRoots.clear();
            }
            options.session.assetRoots.push_back(value);
        } else if (argument == "--allow-origin") {
            std::string value;
            if (!require_value("--allow-origin", value))
                return 2;
            options.allowedOrigins.push_back(value);
        } else if (argument == "--token") {
            if (!require_value("--token", options.token))
                return 2;
        } else {
            std::cerr << "insys_editor_server: unknown argument '" << argument << "'\n";
            print_usage();
            return 2;
        }
    }

    if (options.staticRoot.empty())
        options.staticRoot = default_static_root();

    insys::editor::EditorServer server(std::move(options));
    std::string error;
    if (!server.start(&error)) {
        std::cerr << "insys_editor_server: " << error << "\n";
        return 1;
    }

    g_server = &server;
    std::signal(SIGINT, handle_signal);
#ifdef SIGTERM
    std::signal(SIGTERM, handle_signal);
#endif

    std::cout << "insys editor server listening on http://127.0.0.1:" << server.port()
              << "/?token=" << server.token() << std::endl;
    if (server.port() != 17800) {
        std::cout << "note: Vite proxy expects port 17800; pass --port 17800 or update "
                     "editor/web/vite.config.ts\n";
    }
    const int result = server.run();
    g_server = nullptr;
    return result;
}
