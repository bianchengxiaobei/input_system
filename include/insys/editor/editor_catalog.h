#pragma once

#include <string>
#include <vector>

#include "insys/asset/json.h"

namespace insys::editor {

struct CatalogParameter {
    std::string name;
    std::string displayName;
    std::string type;
    std::string defaultValue;
    std::vector<std::string> options;
};

struct CatalogNamedItem {
    std::string name;
    std::string displayName;
    std::vector<CatalogParameter> parameters;
};

struct CatalogControl {
    std::string name;
    std::string path;
    std::string layout;
    std::string displayName;
    std::vector<std::string> usages;
    std::vector<std::string> aliases;
};

struct CatalogLayout {
    std::string name;
    std::string displayName;
    std::string extends;
    std::vector<CatalogControl> controls;
};

struct CatalogComposite {
    std::string name;
    std::string displayName;
    std::vector<std::string> parts;
    std::vector<CatalogParameter> parameters;
};

struct EditorCatalog {
    std::vector<CatalogLayout> layouts;
    std::vector<std::string> devices;
    std::vector<std::string> controlTypes;
    std::vector<CatalogComposite> composites;
    std::vector<CatalogNamedItem> interactions;
    std::vector<CatalogNamedItem> processors;
};

EditorCatalog build_editor_catalog();
asset::JsonValue catalog_to_json(const EditorCatalog& catalog);

} // namespace insys::editor
