#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "insys/editor/editor_session.h"

namespace insys::editor {

struct EditorServerOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    std::string token;
    std::filesystem::path staticRoot;
    std::vector<std::string> allowedOrigins;
    EditorSessionOptions session;
    std::size_t maxRequestBytes = 16u * 1024u * 1024u;
    std::string serverVersion = "0.1.0";
};

class EditorServer {
public:
    explicit EditorServer(EditorServerOptions options);
    ~EditorServer();

    EditorServer(const EditorServer&) = delete;
    EditorServer& operator=(const EditorServer&) = delete;

    bool start(std::string* error = nullptr);
    int run();
    void stop();

    std::uint16_t port() const;
    const std::string& token() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace insys::editor
