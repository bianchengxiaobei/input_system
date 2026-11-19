#pragma once

#include <filesystem>

#include <RmlUi/Core/FileInterface.h>

namespace insys::ui {

class FileInterface : public Rml::FileInterface {
public:
    void set_root(std::filesystem::path root) { m_root = std::move(root); }
    const std::filesystem::path& root() const { return m_root; }

    Rml::FileHandle Open(const Rml::String& path) override;
    void Close(Rml::FileHandle file) override;
    std::size_t Read(void* buffer, std::size_t size, Rml::FileHandle file) override;
    bool Seek(Rml::FileHandle file, long offset, int origin) override;
    std::size_t Tell(Rml::FileHandle file) override;
    std::size_t Length(Rml::FileHandle file) override;

private:
    std::filesystem::path resolve(const Rml::String& path) const;
    std::filesystem::path m_root;
};

} // namespace insys::ui
