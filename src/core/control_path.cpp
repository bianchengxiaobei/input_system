#include "insys/control_path.h"

#include <algorithm>
#include <cctype>

#include "insys/device.h"
#include "insys/string_utils.h"

namespace insys {
namespace {

bool control_has_usage(const Control& control, InternedString usage) {
    return std::find(control.usages.begin(), control.usages.end(), usage) != control.usages.end();
}

bool control_has_alias(const Control& control, InternedString alias) {
    return std::find(control.aliases.begin(), control.aliases.end(), alias) != control.aliases.end();
}

bool component_matches(const PathComponent& component, const Control& control) {
    if (component.wildcard)
        return true;

    if (!component.layout.empty() &&
        !equals_ignore_case(component.layout.str(), control.layout.str()))
        return false;

    if (!component.name.empty() &&
        !equals_ignore_case(component.name.str(), control.name.str()) &&
        !control_has_alias(control, component.name))
        return false;

    for (InternedString usage : component.usages) {
        if (!control_has_usage(control, usage))
            return false;
    }

    return true;
}

Control* search_descendants(Control* control, const std::vector<PathComponent>& components,
                            std::size_t index) {
    if (index >= components.size())
        return control;

    for (Control* child : control->children) {
        if (component_matches(components[index], *child)) {
            if (Control* result = search_descendants(child, components, index + 1))
                return result;
        }
    }
    return nullptr;
}

} // namespace

std::vector<PathComponent> parse_path(std::string_view path) {
    std::vector<PathComponent> components;

    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        std::string_view token = path.substr(start, end == std::string_view::npos
                                                       ? std::string_view::npos
                                                       : end - start);

        if (token.empty())
            return {};

        PathComponent component;
        while (!token.empty()) {
            if (token.front() == '{') {
                const std::size_t close = token.find('}');
                if (close == std::string_view::npos)
                    return {};
                component.usages.emplace_back(token.substr(1, close - 1));
                token.remove_prefix(close + 1);
            } else if (token.front() == '<') {
                const std::size_t close = token.find('>');
                if (close == std::string_view::npos)
                    return {};
                component.layout = InternedString(token.substr(1, close - 1));
                token.remove_prefix(close + 1);
            } else if (token.front() == '*') {
                component.wildcard = true;
                token.remove_prefix(1);
            } else {
                std::size_t nameEnd = 0;
                while (nameEnd < token.size() && token[nameEnd] != '{' && token[nameEnd] != '<')
                    ++nameEnd;
                component.name = InternedString(token.substr(0, nameEnd));
                token.remove_prefix(nameEnd);
            }
        }

        if (components.empty())
            component.isRoot = true;
        components.push_back(std::move(component));

        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    return components;
}

bool path_matches(std::string_view path, const Control& control) {
    const std::vector<PathComponent> components = parse_path(path);
    if (components.empty())
        return false;

    std::vector<const Control*> chain;
    const Control* cursor = &control;
    while (cursor != nullptr) {
        chain.push_back(cursor);
        cursor = cursor->parent;
    }
    std::reverse(chain.begin(), chain.end());

    if (chain.size() < components.size())
        return false;

    for (std::size_t i = 0; i < components.size(); ++i) {
        if (!component_matches(components[i], *chain[i]))
            return false;
    }
    return true;
}

Control* find_control(Control& root, std::string_view path) {
    const std::vector<PathComponent> components = parse_path(path);
    if (components.empty())
        return nullptr;

    if (!component_matches(components[0], root))
        return nullptr;

    if (components.size() == 1)
        return &root;

    return search_descendants(&root, components, 1);
}

std::string control_path(const Control& control) {
    std::vector<const Control*> chain;
    chain.push_back(&control);
    const Control* cursor = control.parent;
    while (cursor != nullptr && cursor->parent != nullptr) {
        chain.push_back(cursor);
        cursor = cursor->parent;
    }

    std::string result;
    if (control.device != nullptr)
        result = "<" + control.device->layout.str() + ">";
    else if (cursor != nullptr)
        result = "<" + cursor->layout.str() + ">";

    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const Control& item = **it;
        std::string name = item.displayName;
        if (name.empty() || name.find(' ') != std::string::npos ||
            name.find('\t') != std::string::npos) {
            name = item.name.str();
        }
        if (!result.empty())
            result += '/';
        result += name;
    }
    return result;
}

std::string combine_path(const Control* parent, std::string_view name) {
    if (parent == nullptr)
        return std::string(name);
    std::string result = parent->path();
    if (!result.empty())
        result += '/';
    result += name;
    return result;
}

std::string to_display_string(std::string_view path) {
    std::string result(path);
    for (char& character : result) {
        if (character == '<' || character == '>' || character == '{' || character == '}')
            character = ' ';
    }
    return result;
}

} // namespace insys
