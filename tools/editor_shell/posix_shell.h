#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "insys/editor/editor_server.h"

namespace insys::shell {

struct ShellConfig {
    bool devtools = false;
    bool browserOnly = false;
    bool openBrowser = false;
    std::uint32_t smokeMs = 0;
    std::uint16_t remoteDebuggingPort = 0;
    std::string browserPath;
    std::string logPath;
    std::string urlFile;
    std::string userDataDir;
};

struct CommandLine {
    insys::editor::EditorServerOptions server;
    ShellConfig shell;
    bool help = false;
};

class Log {
public:
    bool open(const std::filesystem::path& path);
    void line(std::string_view text);

private:
    std::ofstream m_file;
};

bool parse_command_line(int argc, char** argv, CommandLine& config,
                        std::string& error);
std::filesystem::path default_web_dist();
std::string make_url(const insys::editor::EditorServer& server);
std::string make_origin(const insys::editor::EditorServer& server);
std::string url_encode(std::string_view text);
std::string redact_url(std::string_view url);
void write_url_file(const std::string& path, const std::string& url);
bool open_external_browser(const std::string& browserPath, const std::string& url,
                           std::string& error);
const char* platform_name();

} // namespace insys::shell
