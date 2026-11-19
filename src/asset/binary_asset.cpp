#include "insys/asset/asset_loader.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "insys/actions/action_map.h"

namespace insys::asset {
namespace {

constexpr std::array<char, 8> kMagic = {'I', 'N', 'S', 'Y', 'A', 'S', 'T', '\0'};
constexpr std::uint32_t kBinaryVersion = 1;

constexpr std::size_t kHeaderSize = 36;
constexpr std::size_t kAssetNameOffset = kHeaderSize;
constexpr std::size_t kStringRefSize = 8;
constexpr std::size_t kAssetDataOffset = kHeaderSize + kStringRefSize;

constexpr std::size_t kMapRecordSize = 40;
constexpr std::size_t kActionRecordSize = 60;
constexpr std::size_t kBindingRecordSize = 96;
constexpr std::size_t kSchemeRecordSize = 44;
constexpr std::size_t kDeviceRefSize = 8;

struct StringRef {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

struct MapMeta {
    StringRef name;
    Guid id;
    std::uint32_t actionOffset = 0;
    std::uint32_t actionCount = 0;
    std::uint32_t bindingOffset = 0;
    std::uint32_t bindingCount = 0;
};

struct SchemeMeta {
    StringRef name;
    Guid id;
    StringRef bindingGroup;
    std::uint32_t deviceOffset = 0;
    std::uint32_t deviceCount = 0;
    std::uint32_t flags = 0;
};

[[noreturn]] void binary_fail(const std::string& message) {
    throw AssetError("binary asset: " + message);
}

std::uint32_t checked_u32(std::size_t value, const char* what) {
    if (value > std::numeric_limits<std::uint32_t>::max())
        binary_fail(std::string(what) + " exceeds the 32-bit format limit");
    return static_cast<std::uint32_t>(value);
}

std::uint64_t checked_add(std::uint64_t left, std::uint64_t right, const char* what) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        binary_fail(std::string(what) + " size overflow");
    return left + right;
}

class BinaryWriter {
public:
    BinaryWriter() { m_bytes.resize(kAssetDataOffset, 0); }

    StringRef intern(std::string_view text) {
        if (text.empty())
            return {};
        const auto existing = m_strings.find(std::string(text));
        if (existing != m_strings.end())
            return existing->second;

        const std::uint32_t offset = checked_u32(m_pool.size(), "string pool");
        const std::uint32_t length = checked_u32(text.size(), "string");
        m_pool.append(text.data(), text.size());
        const StringRef ref{offset, length};
        m_strings.emplace(std::string(text), ref);
        return ref;
    }

    void write_u32(std::uint32_t value) {
        m_bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
        m_bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
        m_bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
        m_bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
    }

    void write_u64(std::uint64_t value) {
        write_u32(static_cast<std::uint32_t>(value & 0xffffffffull));
        write_u32(static_cast<std::uint32_t>(value >> 32));
    }

    void write_ref(StringRef ref) {
        write_u32(ref.offset);
        write_u32(ref.length);
    }

    void write_guid(const Guid& id) {
        write_u64(id.high);
        write_u64(id.low);
    }

    void patch_ref(std::size_t offset, StringRef ref) {
        patch_u32(offset, ref.offset);
        patch_u32(offset + 4, ref.length);
    }

    std::string finish(std::uint32_t mapCount, std::uint32_t actionCount,
                       std::uint32_t bindingCount, std::uint32_t schemeCount) {
        const std::uint32_t poolOffset = checked_u32(m_bytes.size(), "string pool offset");
        const std::uint32_t poolSize = checked_u32(m_pool.size(), "string pool");
        m_bytes.insert(m_bytes.end(), m_pool.begin(), m_pool.end());
        write_header(mapCount, actionCount, bindingCount, schemeCount, poolOffset, poolSize);
        return std::string(reinterpret_cast<const char*>(m_bytes.data()), m_bytes.size());
    }

    std::size_t size() const { return m_bytes.size(); }

private:
    void patch_u32(std::size_t offset, std::uint32_t value) {
        if (offset > m_bytes.size() || m_bytes.size() - offset < 4)
            binary_fail("internal header patch out of bounds");
        m_bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
        m_bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
        m_bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
        m_bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
    }

    void write_header(std::uint32_t mapCount, std::uint32_t actionCount,
                      std::uint32_t bindingCount, std::uint32_t schemeCount,
                      std::uint32_t stringPoolOffset, std::uint32_t stringPoolSize) {
        if (m_bytes.size() < kHeaderSize)
            binary_fail("internal header out of bounds");
        std::memcpy(m_bytes.data(), kMagic.data(), kMagic.size());
        patch_u32(8, kBinaryVersion);
        patch_u32(12, stringPoolOffset);
        patch_u32(16, stringPoolSize);
        patch_u32(20, mapCount);
        patch_u32(24, actionCount);
        patch_u32(28, bindingCount);
        patch_u32(32, schemeCount);
    }

    std::vector<std::uint8_t> m_bytes;
    std::string m_pool;
    std::unordered_map<std::string, StringRef> m_strings;
};

class BinaryReader {
public:
    explicit BinaryReader(std::string_view data) : m_data(data) {}

    std::uint32_t read_u32(std::size_t& position) const {
        require(position, 4);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(m_data.data());
        std::uint32_t value = static_cast<std::uint32_t>(bytes[position]) |
                              (static_cast<std::uint32_t>(bytes[position + 1]) << 8) |
                              (static_cast<std::uint32_t>(bytes[position + 2]) << 16) |
                              (static_cast<std::uint32_t>(bytes[position + 3]) << 24);
        position += 4;
        return value;
    }

    std::uint64_t read_u64(std::size_t& position) const {
        const std::uint64_t low = read_u32(position);
        const std::uint64_t high = read_u32(position);
        return low | (high << 32);
    }

    StringRef read_ref(std::size_t& position) const {
        const std::uint32_t offset = read_u32(position);
        const std::uint32_t length = read_u32(position);
        return StringRef{offset, length};
    }

    Guid read_guid(std::size_t& position) const {
        const std::uint64_t high = read_u64(position);
        const std::uint64_t low = read_u64(position);
        return Guid{high, low};
    }

    std::string read_string(StringRef ref, std::string_view pool) const {
        if (ref.length == 0)
            return {};
        if (ref.offset > pool.size() || ref.length > pool.size() - ref.offset)
            binary_fail("string reference is outside the string pool");
        return std::string(pool.substr(ref.offset, ref.length));
    }

    void require(std::size_t position, std::size_t count) const {
        if (position > m_data.size() || m_data.size() - position < count)
            binary_fail("truncated record");
    }

private:
    std::string_view m_data;
};

void write_actions(const ActionAsset& asset, BinaryWriter& writer) {
    for (const ActionMap& map : asset.maps) {
        for (const ActionDef& action : map.actions) {
            writer.write_ref(writer.intern(action.name));
            writer.write_guid(action.id);
            writer.write_u32(static_cast<std::uint32_t>(action.type));
            writer.write_ref(writer.intern(action.expectedControlType));
            writer.write_ref(writer.intern(action.processors));
            writer.write_ref(writer.intern(action.interactions));
            writer.write_u32(action.initialStateCheck ? 1u : 0u);
            writer.write_u32(static_cast<std::uint32_t>(action.priority));
        }
    }
}

void write_bindings(const ActionAsset& asset, BinaryWriter& writer) {
    for (const ActionMap& map : asset.maps) {
        for (const Binding& binding : map.bindings) {
            std::uint32_t flags = 0;
            if (binding.isComposite)
                flags |= 1u;
            if (binding.isPartOfComposite)
                flags |= 2u;

            writer.write_ref(writer.intern(binding.name));
            writer.write_guid(binding.id);
            writer.write_ref(writer.intern(binding.path));
            writer.write_ref(writer.intern(binding.overridePath));
            writer.write_ref(writer.intern(binding.action));
            writer.write_ref(writer.intern(binding.interactions));
            writer.write_ref(writer.intern(binding.processors));
            writer.write_ref(writer.intern(binding.groups));
            writer.write_ref(writer.intern(binding.overrideInteractions));
            writer.write_ref(writer.intern(binding.overrideProcessors));
            writer.write_u32(flags);
            writer.write_u32(static_cast<std::uint32_t>(binding.partIndex));
        }
    }
}

} // namespace

std::string AssetLoader::to_binary(const ActionAsset& asset) {
    std::uint64_t actionCount = 0;
    std::uint64_t bindingCount = 0;
    std::uint64_t deviceCount = 0;
    for (const ActionMap& map : asset.maps) {
        actionCount = checked_add(actionCount, map.actions.size(), "action count");
        bindingCount = checked_add(bindingCount, map.bindings.size(), "binding count");
    }
    for (const ControlScheme& scheme : asset.controlSchemes)
        deviceCount = checked_add(deviceCount, scheme.devices.size(), "device count");

    const std::uint32_t totalActions = checked_u32(actionCount, "action count");
    const std::uint32_t totalBindings = checked_u32(bindingCount, "binding count");
    const std::uint32_t totalDevices = checked_u32(deviceCount, "device count");
    const std::uint32_t mapCount = checked_u32(asset.maps.size(), "map count");
    const std::uint32_t schemeCount = checked_u32(asset.controlSchemes.size(), "scheme count");

    BinaryWriter writer;
    writer.patch_ref(kAssetNameOffset, writer.intern(asset.name));

    std::uint32_t actionCursor = 0;
    std::uint32_t bindingCursor = 0;
    for (const ActionMap& map : asset.maps) {
        writer.write_ref(writer.intern(map.name));
        writer.write_guid(map.id);
        writer.write_u32(actionCursor);
        writer.write_u32(checked_u32(map.actions.size(), "map action count"));
        writer.write_u32(bindingCursor);
        writer.write_u32(checked_u32(map.bindings.size(), "map binding count"));
        actionCursor += static_cast<std::uint32_t>(map.actions.size());
        bindingCursor += static_cast<std::uint32_t>(map.bindings.size());
    }

    write_actions(asset, writer);
    write_bindings(asset, writer);

    std::vector<StringRef> deviceRefs;
    deviceRefs.reserve(totalDevices);
    for (const ControlScheme& scheme : asset.controlSchemes) {
        const std::uint32_t deviceOffset = checked_u32(deviceRefs.size(), "device offset");
        for (const std::string& device : scheme.devices)
            deviceRefs.push_back(writer.intern(device));

        writer.write_ref(writer.intern(scheme.name));
        writer.write_guid(scheme.id);
        writer.write_ref(writer.intern(scheme.bindingGroup));
        writer.write_u32(deviceOffset);
        writer.write_u32(checked_u32(scheme.devices.size(), "scheme device count"));
        writer.write_u32(scheme.isOptional ? 1u : 0u);
    }
    for (StringRef ref : deviceRefs)
        writer.write_ref(ref);

    const std::uint64_t expectedEnd =
        checked_add(kAssetDataOffset,
                    checked_add(checked_add(
                                    checked_add(static_cast<std::uint64_t>(mapCount) * kMapRecordSize,
                                                static_cast<std::uint64_t>(totalActions) * kActionRecordSize,
                                                "map/action records"),
                                    static_cast<std::uint64_t>(totalBindings) * kBindingRecordSize,
                                    "binding records"),
                                checked_add(static_cast<std::uint64_t>(schemeCount) * kSchemeRecordSize,
                                            static_cast<std::uint64_t>(totalDevices) * kDeviceRefSize,
                                            "scheme/device records"),
                                "record bytes"),
                    "record end");
    if (expectedEnd != writer.size())
        binary_fail("internal record layout mismatch");

    return writer.finish(mapCount, totalActions, totalBindings, schemeCount);
}

ActionAsset AssetLoader::load_from_binary(std::string_view binary) {
    if (binary.size() < kAssetDataOffset)
        binary_fail("file is too small");
    if (std::memcmp(binary.data(), kMagic.data(), kMagic.size()) != 0)
        binary_fail("invalid magic");

    BinaryReader reader(binary);
    std::size_t headerPosition = 8;
    const std::uint32_t version = reader.read_u32(headerPosition);
    if (version != kBinaryVersion)
        binary_fail("unsupported version " + std::to_string(version));
    const std::uint32_t stringPoolOffset = reader.read_u32(headerPosition);
    const std::uint32_t stringPoolSize = reader.read_u32(headerPosition);
    const std::uint32_t mapCount = reader.read_u32(headerPosition);
    const std::uint32_t actionCount = reader.read_u32(headerPosition);
    const std::uint32_t bindingCount = reader.read_u32(headerPosition);
    const std::uint32_t schemeCount = reader.read_u32(headerPosition);

    if (stringPoolOffset > binary.size() || stringPoolSize > binary.size() - stringPoolOffset)
        binary_fail("string pool is outside the file");
    const std::string_view pool = binary.substr(stringPoolOffset, stringPoolSize);

    std::size_t assetNamePosition = kAssetNameOffset;
    const StringRef assetName = reader.read_ref(assetNamePosition);

    const std::uint64_t mapRecordEnd = checked_add(kAssetDataOffset,
                                                   static_cast<std::uint64_t>(mapCount) * kMapRecordSize,
                                                   "map records");
    const std::uint64_t actionRecordEnd = checked_add(mapRecordEnd,
                                                      static_cast<std::uint64_t>(actionCount) * kActionRecordSize,
                                                      "action records");
    const std::uint64_t bindingRecordEnd = checked_add(actionRecordEnd,
                                                       static_cast<std::uint64_t>(bindingCount) * kBindingRecordSize,
                                                       "binding records");
    const std::uint64_t schemeRecordEnd = checked_add(bindingRecordEnd,
                                                      static_cast<std::uint64_t>(schemeCount) * kSchemeRecordSize,
                                                      "scheme records");
    if (schemeRecordEnd > stringPoolOffset)
        binary_fail("records overlap the string pool");

    std::vector<MapMeta> maps;
    maps.reserve(mapCount);
    std::size_t mapPosition = kAssetDataOffset;
    for (std::uint32_t i = 0; i < mapCount; ++i) {
        MapMeta meta;
        meta.name = reader.read_ref(mapPosition);
        meta.id = reader.read_guid(mapPosition);
        meta.actionOffset = reader.read_u32(mapPosition);
        meta.actionCount = reader.read_u32(mapPosition);
        meta.bindingOffset = reader.read_u32(mapPosition);
        meta.bindingCount = reader.read_u32(mapPosition);
        if (meta.actionOffset > actionCount || meta.actionCount > actionCount - meta.actionOffset)
            binary_fail("map action range is out of bounds");
        if (meta.bindingOffset > bindingCount ||
            meta.bindingCount > bindingCount - meta.bindingOffset)
            binary_fail("map binding range is out of bounds");
        maps.push_back(meta);
    }

    std::vector<ActionDef> actions;
    actions.reserve(actionCount);
    std::size_t actionPosition = static_cast<std::size_t>(mapRecordEnd);
    for (std::uint32_t i = 0; i < actionCount; ++i) {
        ActionDef action;
        action.name = reader.read_string(reader.read_ref(actionPosition), pool);
        action.id = reader.read_guid(actionPosition);
        const std::uint32_t type = reader.read_u32(actionPosition);
        if (type > static_cast<std::uint32_t>(ActionType::PassThrough))
            binary_fail("invalid action type");
        action.type = static_cast<ActionType>(type);
        action.expectedControlType = reader.read_string(reader.read_ref(actionPosition), pool);
        action.processors = reader.read_string(reader.read_ref(actionPosition), pool);
        action.interactions = reader.read_string(reader.read_ref(actionPosition), pool);
        const std::uint32_t initialStateCheck = reader.read_u32(actionPosition);
        if (initialStateCheck > 1)
            binary_fail("invalid initialStateCheck value");
        action.initialStateCheck = initialStateCheck != 0;
        action.priority = static_cast<std::int32_t>(reader.read_u32(actionPosition));
        actions.push_back(std::move(action));
    }

    std::vector<Binding> bindings;
    bindings.reserve(bindingCount);
    std::size_t bindingPosition = static_cast<std::size_t>(actionRecordEnd);
    for (std::uint32_t i = 0; i < bindingCount; ++i) {
        Binding binding;
        binding.name = reader.read_string(reader.read_ref(bindingPosition), pool);
        binding.id = reader.read_guid(bindingPosition);
        binding.path = reader.read_string(reader.read_ref(bindingPosition), pool);
        binding.overridePath = reader.read_string(reader.read_ref(bindingPosition), pool);
        binding.action = reader.read_string(reader.read_ref(bindingPosition), pool);
        binding.interactions = reader.read_string(reader.read_ref(bindingPosition), pool);
        binding.processors = reader.read_string(reader.read_ref(bindingPosition), pool);
        binding.groups = reader.read_string(reader.read_ref(bindingPosition), pool);
        binding.overrideInteractions = reader.read_string(reader.read_ref(bindingPosition), pool);
        binding.overrideProcessors = reader.read_string(reader.read_ref(bindingPosition), pool);
        const std::uint32_t flags = reader.read_u32(bindingPosition);
        if ((flags & ~3u) != 0)
            binary_fail("invalid binding flags");
        binding.isComposite = (flags & 1u) != 0;
        binding.isPartOfComposite = (flags & 2u) != 0;
        binding.partIndex = static_cast<std::int32_t>(reader.read_u32(bindingPosition));
        bindings.push_back(std::move(binding));
    }

    std::vector<SchemeMeta> schemes;
    schemes.reserve(schemeCount);
    std::size_t schemePosition = static_cast<std::size_t>(bindingRecordEnd);
    std::uint64_t deviceTotal = 0;
    for (std::uint32_t i = 0; i < schemeCount; ++i) {
        SchemeMeta meta;
        meta.name = reader.read_ref(schemePosition);
        meta.id = reader.read_guid(schemePosition);
        meta.bindingGroup = reader.read_ref(schemePosition);
        meta.deviceOffset = reader.read_u32(schemePosition);
        meta.deviceCount = reader.read_u32(schemePosition);
        meta.flags = reader.read_u32(schemePosition);
        if ((meta.flags & ~1u) != 0)
            binary_fail("invalid scheme flags");
        schemes.push_back(meta);
        deviceTotal = checked_add(deviceTotal, meta.deviceCount, "device count");
    }

    const std::uint64_t deviceRecordEnd =
        checked_add(schemeRecordEnd, deviceTotal * kDeviceRefSize, "device records");
    if (deviceRecordEnd > stringPoolOffset)
        binary_fail("device references overlap the string pool");

    std::vector<StringRef> deviceRefs;
    deviceRefs.reserve(static_cast<std::size_t>(deviceTotal));
    std::size_t devicePosition = static_cast<std::size_t>(schemeRecordEnd);
    for (std::uint64_t i = 0; i < deviceTotal; ++i)
        deviceRefs.push_back(reader.read_ref(devicePosition));

    ActionAsset asset;
    asset.name = reader.read_string(assetName, pool);
    for (const MapMeta& meta : maps) {
        ActionMap& map = asset.add_map(reader.read_string(meta.name, pool));
        map.id = meta.id;
        for (std::uint32_t i = 0; i < meta.actionCount; ++i)
            map.actions.push_back(std::move(actions[meta.actionOffset + i]));
        for (std::uint32_t i = 0; i < meta.bindingCount; ++i)
            map.bindings.push_back(std::move(bindings[meta.bindingOffset + i]));
    }

    for (const SchemeMeta& meta : schemes) {
        if (meta.deviceOffset > deviceTotal || meta.deviceCount > deviceTotal - meta.deviceOffset)
            binary_fail("scheme device range is out of bounds");
        ControlScheme scheme;
        scheme.name = reader.read_string(meta.name, pool);
        scheme.id = meta.id;
        scheme.bindingGroup = reader.read_string(meta.bindingGroup, pool);
        scheme.isOptional = (meta.flags & 1u) != 0;
        scheme.devices.reserve(meta.deviceCount);
        for (std::uint32_t i = 0; i < meta.deviceCount; ++i)
            scheme.devices.push_back(
                reader.read_string(deviceRefs[meta.deviceOffset + i], pool));
        asset.controlSchemes.push_back(std::move(scheme));
    }

    return asset;
}

} // namespace insys::asset
