#include "posix_shell.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace insys::shell {

namespace {


std::filesystem::path executable_directory() {
#ifdef __APPLE__
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return std::filesystem::current_path();
    std::error_code error;
    std::filesystem::path path =
        std::filesystem::weakly_canonical(buffer.data(), error);
    if (error)
        return std::filesystem::path(buffer.data()).parent_path();
    return path.parent_path();
#else
    std::vector<char> buffer(4096, '\0');
    const ssize_t length =
        readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0)
        return std::filesystem::current_path();
    return std::filesystem::path(std::string(buffer.data(), length))
        .parent_path();
#endif
}

bool parse_unsigned(const std::string& text, unsigned long& value) {
    if (text.empty())
        return false;
    try {
        std::size_t consumed = 0;
        value = std::stoul(text, &consumed, 10);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

} // namespace

bool Log::open(const std::filesystem::path& path) {
    if (path.empty())
        return true;
    std::error_code error;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, error);
    m_file.open(path, std::ios::out | std::ios::app);
    return m_file.is_open();
}

void Log::line(std::string_view text) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char stamp[16]{};
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
    std::ostringstream message;
    message << "[" << stamp << "] " << text << "\n";
    const std::string output = message.str();
    std::cerr << output;
    if (m_file.is_open()) {
        m_file << output;
        m_file.flush();
    }
}

std::string url_encode(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(text.size());
    for (const unsigned char character : text) {
        const bool unreserved =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.' ||
            character == '~';
        if (unreserved) {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('%');
            result.push_back(kHex[(character >> 4u) & 0x0fu]);
            result.push_back(kHex[character & 0x0fu]);
        }
    }
    return result;
}

std::string redact_url(std::string_view url) {
    const std::size_t token = url.find("token=");
    if (token == std::string_view::npos)
        return std::string(url);
    return std::string(url.substr(0, token)) + "token=<redacted>";
}

std::filesystem::path default_web_dist() {
    const std::filesystem::path exe = executable_directory();
    const std::filesystem::path candidates[] = {
#ifdef __APPLE__
        exe / ".." / "Resources" / "web",
#endif
        exe / "web",
        std::filesystem::current_path() / "editor" / "web" / "dist",
    };
    std::error_code error;
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::is_directory(candidate, error))
            return candidate;
    }
#ifdef INSYS_EDITOR_WEB_DIST
    const std::filesystem::path configured(INSYS_EDITOR_WEB_DIST);
    if (std::filesystem::is_directory(configured, error))
        return configured;
#endif
    return {};
}

std::string make_url(const insys::editor::EditorServer& server) {
    return "http://127.0.0.1:" + std::to_string(server.port()) +
           "/?token=" + url_encode(server.token());
}

std::string make_origin(const insys::editor::EditorServer& server) {
    return "http://127.0.0.1:" + std::to_string(server.port()) + "/";
}

void write_url_file(const std::string& path, const std::string& url) {
    if (path.empty())
        return;
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (file.is_open())
        file << url << "\n";
}


bool parse_command_line(int argc, char** argv, CommandLine& config,
                        std::string& error) {
    config.server.host = "127.0.0.1";
    config.server.port = 0;
    config.server.serverVersion = "0.1.0";
    config.server.session.assetRoots.push_back(std::filesystem::current_path());

    bool hasAssetRoot = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto require_value = [&](std::string& value) {
            if (i + 1 >= argc) {
                error = "missing value for " + argument;
                return false;
            }
            value = argv[++i];
            return true;
        };

        if (argument == "--help" || argument == "-h") {
            config.help = true;
        } else if (argument == "--port") {
            std::string value;
            if (!require_value(value))
                return false;
            unsigned long port = 0;
            if (!parse_unsigned(value, port) || port > 65535) {
                error = "invalid port";
                return false;
            }
            config.server.port = static_cast<std::uint16_t>(port);
        } else if (argument == "--static") {
            std::string value;
            if (!require_value(value))
                return false;
            config.server.staticRoot = std::filesystem::path(value);
        } else if (argument == "--asset-root") {
            std::string value;
            if (!require_value(value))
                return false;
            if (!hasAssetRoot) {
                config.server.session.assetRoots.clear();
                hasAssetRoot = true;
            }
            config.server.session.assetRoots.push_back(
                std::filesystem::path(value));
        } else if (argument == "--token") {
            std::string value;
            if (!require_value(value))
                return false;
            config.server.token = value;
        } else if (argument == "--user-data-dir") {
            if (!require_value(config.shell.userDataDir))
                return false;
        } else if (argument == "--remote-debugging-port") {
            std::string value;
            if (!require_value(value))
                return false;
            unsigned long port = 0;
            if (!parse_unsigned(value, port) || port == 0 || port > 65535) {
                error = "invalid --remote-debugging-port";
                return false;
            }
            config.shell.remoteDebuggingPort =
                static_cast<std::uint16_t>(port);
        } else if (argument == "--devtools") {
            config.shell.devtools = true;
        } else if (argument == "--browser-only") {
            config.shell.browserOnly = true;
        } else if (argument == "--open-browser") {
            config.shell.openBrowser = true;
        } else if (argument == "--browser") {
            if (!require_value(config.shell.browserPath))
                return false;
        } else if (argument == "--url-file") {
            if (!require_value(config.shell.urlFile))
                return false;
        } else if (argument == "--log") {
            if (!require_value(config.shell.logPath))
                return false;
        } else if (argument == "--smoke-ms") {
            std::string value;
            if (!require_value(value))
                return false;
            unsigned long milliseconds = 0;
            if (!parse_unsigned(value, milliseconds) || milliseconds == 0 ||
                milliseconds > 60000) {
                error = "invalid --smoke-ms value";
                return false;
            }
            config.shell.smokeMs = static_cast<std::uint32_t>(milliseconds);
        } else {
            error = "unknown argument: " + argument;
            return false;
        }
    }

    if (config.server.staticRoot.empty())
        config.server.staticRoot = default_web_dist();
    return true;
}

bool open_external_browser(const std::string& browserPath,
                           const std::string& url, std::string& error) {
    const pid_t first = fork();
    if (first < 0) {
        error = std::strerror(errno);
        return false;
    }
    if (first == 0) {
        const pid_t second = fork();
        if (second < 0)
            _exit(127);
        if (second > 0)
            _exit(0);
        setsid();
#ifdef __APPLE__
        if (browserPath.empty()) {
            execl("/usr/bin/open", "open", url.c_str(),
                  static_cast<char*>(nullptr));
        } else {
            execl("/usr/bin/open", "open", "-a", browserPath.c_str(),
                  url.c_str(), static_cast<char*>(nullptr));
        }
#else
        if (browserPath.empty()) {
            execlp("xdg-open", "xdg-open", url.c_str(),
                   static_cast<char*>(nullptr));
        } else {
            execl(browserPath.c_str(), browserPath.c_str(), url.c_str(),
                  static_cast<char*>(nullptr));
        }
#endif
        _exit(127);
    }
    int status = 0;
    waitpid(first, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "browser launcher failed";
        return false;
    }
    return true;
}

const char* platform_name() {
#ifdef __APPLE__
    return "macOS";
#else
    return "Linux";
#endif
}

} // namespace insys::shell
