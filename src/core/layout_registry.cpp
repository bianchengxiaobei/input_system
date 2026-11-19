#include "insys/layout_registry.h"

#include <algorithm>

#include "insys/string_utils.h"

namespace insys {

std::string LayoutRegistry::normalize(std::string_view name) {
    const std::size_t namespaceSeparator = name.find(':');
    if (namespaceSeparator != std::string_view::npos)
        name = name.substr(0, namespaceSeparator);
    return to_lower(name);
}

Layout* LayoutRegistry::add(std::unique_ptr<Layout> layout) {
    if (!layout)
        return nullptr;

    const std::string key = normalize(layout->name);
    Layout* raw = layout.get();
    m_layouts[key] = std::move(layout);
    return raw;
}

Layout* LayoutRegistry::resolve(std::string_view name) {
    const std::string key = normalize(name);
    const auto it = m_layouts.find(key);
    if (it == m_layouts.end())
        return nullptr;

    Layout* layout = it->second.get();
    if (layout->baseLayout != nullptr || layout->extends.empty())
        return layout;

    Layout* base = resolve(layout->extends);
    if (base == nullptr || base == layout)
        return layout;

    if (base->baseLayout == nullptr && !base->extends.empty()) {
        Layout* resolvedBase = resolve(base->extends);
        if (resolvedBase != nullptr && resolvedBase != base)
            base->merge_from(*resolvedBase);
    }

    layout->merge_from(*base);
    return layout;
}

Layout* LayoutRegistry::find(std::string_view name) {
    return resolve(name);
}

const Layout* LayoutRegistry::find(std::string_view name) const {
    return const_cast<LayoutRegistry*>(this)->resolve(name);
}

void LayoutRegistry::add_matcher(std::string_view layoutName, const DeviceMatcher& matcher) {
    m_matchers.emplace_back(normalize(layoutName), matcher);
}

std::string LayoutRegistry::find_matching_layout(const DeviceDescription& description) const {
    float bestScore = 0.0f;
    std::string bestLayout;

    for (const auto& [layoutName, matcher] : m_matchers) {
        const float score = matcher.match_score(description);
        if (score > bestScore) {
            bestScore = score;
            bestLayout = layoutName;
        }
    }

    return bestLayout;
}

std::vector<std::string> LayoutRegistry::list() const {
    std::vector<std::string> names;
    names.reserve(m_layouts.size());
    for (const auto& [name, layout] : m_layouts) {
        (void)layout;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace insys
