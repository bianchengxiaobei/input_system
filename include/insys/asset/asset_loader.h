#pragma once

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "insys/actions/action_asset.h"

namespace insys::asset {

class AssetError : public std::runtime_error {
public:
    AssetError(std::string message, std::size_t line = 0, std::size_t column = 0)
        : std::runtime_error(std::move(message)), m_line(line), m_column(column) {}

    std::size_t line() const { return m_line; }
    std::size_t column() const { return m_column; }

private:
    std::size_t m_line = 0;
    std::size_t m_column = 0;
};

class AssetLoader {
public:
    static ActionAsset load_from_file(const std::filesystem::path& path);
    static ActionAsset load_from_json(std::string_view json);
    static ActionAsset load_from_binary(std::string_view binary);
    static std::string to_json(const ActionAsset& asset);
    static std::string to_binary(const ActionAsset& asset);
};

} // namespace insys::asset
