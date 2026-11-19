#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "insys/editor/editor_session.h"

namespace insys::editor {

class HidToolService;
class PreviewRuntime;

struct EditorProtocolOptions {
    std::string serverVersion = "0.1.0";
    std::string protocolVersion = "1";
    std::size_t maxMessageBytes = 16u * 1024u * 1024u;
    bool enablePreviewHid = false;
    std::function<void()> requestShutdown;
};

class EditorProtocol {
public:
    explicit EditorProtocol(const EditorSession& session,
                            EditorProtocolOptions options = {});
    ~EditorProtocol();

    std::optional<std::string> handle(std::string_view message) const;

private:
    const EditorSession& m_session;
    EditorProtocolOptions m_options;
    mutable std::unique_ptr<PreviewRuntime> m_preview;
    mutable std::unique_ptr<HidToolService> m_hidTools;
};

} // namespace insys::editor
