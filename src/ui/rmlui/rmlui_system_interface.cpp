#include "insys/ui/rmlui_system_interface.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace insys::ui {
namespace {

#ifdef _WIN32
std::string utf16_to_utf8(const std::wstring& text) {
    if (text.empty())
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring utf8_to_utf16(const std::string& text) {
    if (text.empty())
        return {};

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    if (size <= 0)
        return {};

    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size);
    return result;
}
#endif

} // namespace

RmlUiSystemInterface::RmlUiSystemInterface(ITime& time) : m_time(time) {}

void RmlUiSystemInterface::set_mouse_cursor_callback(MouseCursorCallback callback) {
    m_mouseCursorCallback = std::move(callback);
}

double RmlUiSystemInterface::GetElapsedTime() {
    return m_time.now();
}

bool RmlUiSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    const char* prefix = "info";
    switch (type) {
    case Rml::Log::LT_ERROR: prefix = "error"; break;
    case Rml::Log::LT_ASSERT: prefix = "assert"; break;
    case Rml::Log::LT_WARNING: prefix = "warning"; break;
    case Rml::Log::LT_INFO: prefix = "info"; break;
    case Rml::Log::LT_DEBUG: prefix = "debug"; break;
    default: break;
    }

    std::ostream& out = (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT)
                            ? std::cerr
                            : std::cout;
    out << "[RmlUi:" << prefix << "] " << message << '\n';
    return true;
}

void RmlUiSystemInterface::SetMouseCursor(const Rml::String& cursorName) {
    m_lastMouseCursor = cursorName.c_str();
    if (m_mouseCursorCallback)
        m_mouseCursorCallback(m_lastMouseCursor);
}

void RmlUiSystemInterface::GetClipboardText(Rml::String& text) {
#ifdef _WIN32
    text.clear();
    if (!OpenClipboard(nullptr))
        return;

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle != nullptr) {
        auto* data = static_cast<const wchar_t*>(GlobalLock(handle));
        if (data != nullptr) {
            text = utf16_to_utf8(std::wstring(data));
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
#endif
}

void RmlUiSystemInterface::SetClipboardText(const Rml::String& text) {
#ifdef _WIN32
    if (!OpenClipboard(nullptr))
        return;

    EmptyClipboard();

    const std::wstring wide = utf8_to_utf16(text);
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (handle != nullptr) {
        auto* data = static_cast<wchar_t*>(GlobalLock(handle));
        if (data != nullptr) {
            std::copy(wide.begin(), wide.end(), data);
            data[wide.size()] = L'\0';
            GlobalUnlock(handle);
            if (SetClipboardData(CF_UNICODETEXT, handle) == nullptr)
                GlobalFree(handle);
        } else {
            GlobalFree(handle);
        }
    }

    CloseClipboard();
#endif
}

void RmlUiSystemInterface::ActivateKeyboard(Rml::Vector2f, float) {}

void RmlUiSystemInterface::DeactivateKeyboard() {}

} // namespace insys::ui
