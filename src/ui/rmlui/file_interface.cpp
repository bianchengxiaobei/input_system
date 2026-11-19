#include "insys/ui/file_interface.h"

#include <fstream>

namespace insys::ui {

std::filesystem::path FileInterface::resolve(const Rml::String& path) const {
    std::filesystem::path candidate = std::filesystem::u8path(path);
    if (candidate.is_absolute() || m_root.empty())
        return candidate;
    return m_root / candidate;
}

Rml::FileHandle FileInterface::Open(const Rml::String& path) {
    auto stream = std::make_unique<std::ifstream>(resolve(path), std::ios::binary);
    if (!stream->is_open())
        return 0;
    return reinterpret_cast<Rml::FileHandle>(stream.release());
}

void FileInterface::Close(Rml::FileHandle file) {
    if (file == 0)
        return;
    delete reinterpret_cast<std::ifstream*>(file);
}

std::size_t FileInterface::Read(void* buffer, std::size_t size, Rml::FileHandle file) {
    if (file == 0)
        return 0;
    auto* stream = reinterpret_cast<std::ifstream*>(file);
    stream->read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return static_cast<std::size_t>(stream->gcount());
}

bool FileInterface::Seek(Rml::FileHandle file, long offset, int origin) {
    if (file == 0)
        return false;

    auto* stream = reinterpret_cast<std::ifstream*>(file);
    stream->clear();

    std::ios_base::seekdir direction = std::ios_base::beg;
    if (origin == SEEK_CUR)
        direction = std::ios_base::cur;
    else if (origin == SEEK_END)
        direction = std::ios_base::end;

    stream->seekg(static_cast<std::streamoff>(offset), direction);
    return static_cast<bool>(*stream);
}

std::size_t FileInterface::Tell(Rml::FileHandle file) {
    if (file == 0)
        return 0;
    auto* stream = reinterpret_cast<std::ifstream*>(file);
    const auto position = stream->tellg();
    return position < 0 ? 0 : static_cast<std::size_t>(position);
}

std::size_t FileInterface::Length(Rml::FileHandle file) {
    if (file == 0)
        return 0;

    auto* stream = reinterpret_cast<std::ifstream*>(file);
    const auto current = stream->tellg();
    stream->seekg(0, std::ios_base::end);
    const auto end = stream->tellg();
    stream->seekg(current);
    return end < 0 ? 0 : static_cast<std::size_t>(end);
}

} // namespace insys::ui
