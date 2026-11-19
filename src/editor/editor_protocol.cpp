#include "insys/editor/editor_protocol.h"

#include <cmath>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "insys/asset/asset_loader.h"
#include "insys/asset/json.h"
#include "insys/editor/editor_catalog.h"
#include "insys/editor/editor_hid_tools.h"
#include "insys/editor/editor_preview.h"
#include "insys/string_utils.h"

namespace insys::editor {
namespace {

using JsonValue = asset::JsonValue;

constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;
constexpr int kValidationFailed = -32010;
constexpr int kRevisionConflict = -32011;
constexpr int kRequestTooLarge = -32013;
constexpr int kPathDenied = -32020;
constexpr int kNotFound = -32021;
constexpr int kUnsupportedFormat = -32022;
constexpr int kFileTooLarge = -32023;
constexpr int kIoError = -32024;
constexpr int kSchemaUnsupported = -32025;

JsonValue make_object() {
    JsonValue value;
    value.type = JsonValue::Type::Object;
    return value;
}

JsonValue make_array() {
    JsonValue value;
    value.type = JsonValue::Type::Array;
    return value;
}

JsonValue make_string(std::string value) {
    JsonValue result;
    result.type = JsonValue::Type::String;
    result.string = std::move(value);
    return result;
}

JsonValue make_number(double value) {
    JsonValue result;
    result.type = JsonValue::Type::Number;
    result.number = value;
    return result;
}

JsonValue make_bool(bool value) {
    JsonValue result;
    result.type = JsonValue::Type::Bool;
    result.boolean = value;
    return result;
}

JsonValue make_null() {
    return JsonValue{};
}

void add(JsonValue& object, std::string key, JsonValue value) {
    object.object.emplace_back(std::move(key), std::move(value));
}

std::string severity_name(EditorDiagnostic::Severity severity) {
    switch (severity) {
    case EditorDiagnostic::Severity::Warning: return "warning";
    case EditorDiagnostic::Severity::Info: return "info";
    case EditorDiagnostic::Severity::Error:
    default: return "error";
    }
}

JsonValue diagnostic_json(const EditorDiagnostic& diagnostic) {
    JsonValue result = make_object();
    add(result, "severity", make_string(severity_name(diagnostic.severity)));
    add(result, "code", make_string(diagnostic.code));
    add(result, "message", make_string(diagnostic.message));
    if (!diagnostic.jsonPointer.empty())
        add(result, "jsonPointer", make_string(diagnostic.jsonPointer));
    if (diagnostic.line != 0)
        add(result, "line", make_number(static_cast<double>(diagnostic.line)));
    if (diagnostic.column != 0)
        add(result, "column", make_number(static_cast<double>(diagnostic.column)));
    return result;
}

JsonValue diagnostics_json(const std::vector<EditorDiagnostic>& diagnostics) {
    JsonValue result = make_array();
    for (const EditorDiagnostic& diagnostic : diagnostics)
        result.array.push_back(diagnostic_json(diagnostic));
    return result;
}

JsonValue error_data(const std::vector<EditorDiagnostic>& diagnostics) {
    JsonValue result = make_object();
    add(result, "diagnostics", diagnostics_json(diagnostics));
    return result;
}

std::string response_with_error(const JsonValue& id, int code,
                                const std::string& message,
                                const std::vector<EditorDiagnostic>& diagnostics = {}) {
    JsonValue error = make_object();
    add(error, "code", make_number(code));
    add(error, "message", make_string(message));
    if (!diagnostics.empty())
        add(error, "data", error_data(diagnostics));

    JsonValue response = make_object();
    add(response, "jsonrpc", make_string("2.0"));
    add(response, "id", id);
    add(response, "error", std::move(error));
    return asset::write_json(response);
}

std::string response_with_result(const JsonValue& id, JsonValue result) {
    JsonValue response = make_object();
    add(response, "jsonrpc", make_string("2.0"));
    add(response, "id", id);
    add(response, "result", std::move(result));
    return asset::write_json(response);
}

int error_code_for(const EditorError& error) {
    const std::string& code = error.code();
    if (code == "validation_failed")
        return kValidationFailed;
    if (code == "revision_conflict")
        return kRevisionConflict;
    if (code == "path_denied" || code == "invalid_path")
        return kPathDenied;
    if (code == "not_found")
        return kNotFound;
    if (code == "unsupported_format")
        return kUnsupportedFormat;
    if (code == "file_too_large")
        return kFileTooLarge;
    if (code == "io_error")
        return kIoError;
    if (code == "schema_unsupported")
        return kSchemaUnsupported;
    return kInternalError;
}

const JsonValue& params_or_empty(const JsonValue* params, JsonValue& empty) {
    return params != nullptr ? *params : empty;
}

const JsonValue* param(const JsonValue& params, std::string_view name) {
    return params.find(name);
}

std::string required_string(const JsonValue& params, std::string_view name,
                            std::string& error) {
    const JsonValue* value = param(params, name);
    if (value == nullptr) {
        error = "missing parameter '" + std::string(name) + "'";
        return {};
    }
    if (!value->is_string()) {
        error = "parameter '" + std::string(name) + "' must be a string";
        return {};
    }
    return value->string;
}

bool optional_bool(const JsonValue& params, std::string_view name, bool fallback,
                   bool& out) {
    const JsonValue* value = param(params, name);
    if (value == nullptr) {
        out = fallback;
        return true;
    }
    if (!value->is_bool())
        return false;
    out = value->boolean;
    return true;
}

bool optional_string(const JsonValue& params, std::string_view name,
                     std::string& out, std::string& error) {
    const JsonValue* value = param(params, name);
    if (value == nullptr)
        return true;
    if (!value->is_string()) {
        error = "parameter '" + std::string(name) + "' must be a string";
        return false;
    }
    out = value->string;
    return true;
}

bool optional_number(const JsonValue& params, std::string_view name, double fallback,
                     double& out, std::string& error) {
    const JsonValue* value = param(params, name);
    if (value == nullptr) {
        out = fallback;
        return true;
    }
    if (!value->is_number()) {
        error = "parameter '" + std::string(name) + "' must be a number";
        return false;
    }
    out = value->number;
    return true;
}

bool optional_generation(const JsonValue& params, std::uint64_t& out,
                         std::string& error) {
    const JsonValue* value = param(params, "generation");
    if (value == nullptr)
        return true;
    if (!value->is_number() || value->number < 0.0 ||
        value->number != std::floor(value->number) ||
        value->number > 9007199254740991.0) {
        error = "parameter 'generation' must be a non-negative integer";
        return false;
    }
    out = static_cast<std::uint64_t>(value->number);
    return true;
}

JsonValue control_paths_json(const EditorCatalog& catalog, std::string query,
                             std::string expectedControlType,
                             std::string deviceFilter) {
    query = to_lower(query);
    expectedControlType = to_lower(expectedControlType);
    deviceFilter = to_lower(deviceFilter);
    JsonValue result = make_array();
    for (const CatalogLayout& layout : catalog.layouts) {
        if (!deviceFilter.empty() && to_lower(layout.name) != deviceFilter)
            continue;
        for (const CatalogControl& control : layout.controls) {
            if (!expectedControlType.empty() &&
                to_lower(control.layout) != expectedControlType)
                continue;
            if (!query.empty()) {
                const std::string haystack =
                    to_lower(control.path + " " + control.layout + " " + control.name);
                if (haystack.find(query) == std::string::npos)
                    continue;
            }
            result.array.push_back(make_string(control.path));
        }
    }
    return result;
}

const char* hid_report_kind_name(backend::HidReportKind kind) {
    switch (kind) {
    case backend::HidReportKind::Output: return "output";
    case backend::HidReportKind::Feature: return "feature";
    case backend::HidReportKind::Input:
    default: return "input";
    }
}

const char* hid_transport_name(hid::HidTransport transport) {
    switch (transport) {
    case hid::HidTransport::Usb: return "usb";
    case hid::HidTransport::Bluetooth: return "bluetooth";
    case hid::HidTransport::Other: return "other";
    case hid::HidTransport::Unknown:
    default: return "unknown";
    }
}

const char* hid_kind_name(hid::DeviceKind kind) {
    switch (kind) {
    case hid::DeviceKind::DualShock3: return "DualShock3";
    case hid::DeviceKind::DualShock4: return "DualShock4";
    case hid::DeviceKind::DualSense: return "DualSense";
    case hid::DeviceKind::SwitchPro: return "SwitchPro";
    case hid::DeviceKind::SteamController: return "SteamController";
    case hid::DeviceKind::SteamDeck: return "SteamDeck";
    case hid::DeviceKind::Generic:
    default: return "Generic";
    }
}

const char* hid_report_type_name(hid::ReportType type) {
    switch (type) {
    case hid::ReportType::Output: return "output";
    case hid::ReportType::Feature: return "feature";
    case hid::ReportType::Input:
    default: return "input";
    }
}

JsonValue bytes_json(const std::vector<std::uint8_t>& bytes) {
    JsonValue result = make_array();
    result.array.reserve(bytes.size());
    for (std::uint8_t byte : bytes)
        result.array.push_back(make_number(static_cast<double>(byte)));
    return result;
}

JsonValue hid_device_json(const backend::DeviceView& view) {
    JsonValue result = make_object();
    add(result, "id", make_string(view.id));
    add(result, "product", make_string(view.info.description.product));
    add(result, "manufacturer", make_string(view.info.description.manufacturer));
    add(result, "serial", make_string(view.info.description.serial));
    add(result, "vendorId", make_number(static_cast<double>(view.info.vendorId)));
    add(result, "productId", make_number(static_cast<double>(view.info.productId)));
    add(result, "usagePage", make_number(static_cast<double>(view.info.usagePage)));
    add(result, "usage", make_number(static_cast<double>(view.info.usage)));
    add(result, "transport", make_string(hid_transport_name(view.info.transport)));
    add(result, "kind", make_string(hid_kind_name(view.kind)));
    add(result, "layoutName", make_string(view.layoutName));
    add(result, "inputReportSize", make_number(static_cast<double>(view.info.inputReportSize)));
    add(result, "outputReportSize", make_number(static_cast<double>(view.info.outputReportSize)));
    add(result, "featureReportSize", make_number(static_cast<double>(view.info.featureReportSize)));
    add(result, "hasDescriptor", make_bool(!view.info.reportDescriptor.empty()));

    const bool hasHaptics = view.kind != hid::DeviceKind::Generic;
    JsonValue capabilities = make_object();
    add(capabilities, "haptics", make_bool(hasHaptics));
    add(capabilities, "lightbar", make_bool(view.kind == hid::DeviceKind::DualShock4 ||
                                             view.kind == hid::DeviceKind::DualSense));
    add(capabilities, "adaptiveTrigger", make_bool(view.kind == hid::DeviceKind::DualSense));
    add(capabilities, "switchSubcommand", make_bool(view.kind == hid::DeviceKind::SwitchPro));
    add(result, "capabilities", std::move(capabilities));
    return result;
}

JsonValue hid_element_json(const hid::Element& element) {
    JsonValue result = make_object();
    add(result, "usage", make_number(static_cast<double>(element.usage)));
    add(result, "usagePage", make_number(static_cast<double>(element.usagePage)));
    add(result, "logicalMin", make_number(static_cast<double>(element.logicalMin)));
    add(result, "logicalMax", make_number(static_cast<double>(element.logicalMax)));
    add(result, "physicalMin", make_number(static_cast<double>(element.physicalMin)));
    add(result, "physicalMax", make_number(static_cast<double>(element.physicalMax)));
    add(result, "unit", make_number(static_cast<double>(element.unit)));
    add(result, "unitExponent", make_number(static_cast<double>(element.unitExponent)));
    add(result, "reportType", make_string(hid_report_type_name(element.reportType)));
    add(result, "collectionIndex", make_number(static_cast<double>(element.collectionIndex)));
    add(result, "reportId", make_number(static_cast<double>(element.reportId)));
    add(result, "reportSizeInBits", make_number(static_cast<double>(element.reportSizeInBits)));
    add(result, "reportOffsetInBits", make_number(static_cast<double>(element.reportOffsetInBits)));
    add(result, "flags", make_number(static_cast<double>(element.flags)));
    add(result, "isButton", make_bool(element.usagePage == hid::UsagePage::Button ||
                                      (element.usagePage == hid::UsagePage::GenericDesktop &&
                                       (element.usage == 0x3D || element.usage == 0x3E ||
                                        (element.usage >= 0x90 && element.usage <= 0x93)))));
    add(result, "isAxis", make_bool(!element.is_constant() && !element.is_array() &&
                                    element.reportSizeInBits > 1));
    add(result, "isArray", make_bool(element.is_array()));
    add(result, "isConstant", make_bool(element.is_constant()));
    add(result, "isRelative", make_bool(element.is_relative()));
    add(result, "hasNullState", make_bool(element.has_null_state()));
    return result;
}

JsonValue hid_report_info_json(const hid::ReportInfo& report) {
    JsonValue result = make_object();
    add(result, "id", make_number(static_cast<double>(report.id)));
    add(result, "hasId", make_bool(report.hasId));
    add(result, "type", make_string(hid_report_type_name(report.type)));
    add(result, "sizeInBits", make_number(static_cast<double>(report.sizeInBits)));
    add(result, "sizeInBytes", make_number(static_cast<double>(report.size_in_bytes())));
    return result;
}

JsonValue hid_collection_json(const hid::Collection& collection) {
    JsonValue result = make_object();
    add(result, "type", make_number(static_cast<double>(collection.type)));
    add(result, "usage", make_number(static_cast<double>(collection.usage)));
    add(result, "usagePage", make_number(static_cast<double>(collection.usagePage)));
    add(result, "parent", make_number(static_cast<double>(collection.parent)));
    add(result, "firstChild", make_number(static_cast<double>(collection.firstChild)));
    add(result, "childCount", make_number(static_cast<double>(collection.childCount)));
    return result;
}

JsonValue hid_control_json(const HidControlView& control) {
    JsonValue result = make_object();
    add(result, "path", make_string(control.path));
    add(result, "name", make_string(control.name));
    add(result, "layout", make_string(control.layout));
    add(result, "byteOffset", make_number(static_cast<double>(control.byteOffset)));
    add(result, "bitOffset", make_number(static_cast<double>(control.bitOffset)));
    add(result, "sizeInBits", make_number(static_cast<double>(control.sizeInBits)));
    add(result, "isButton", make_bool(control.isButton));
    add(result, "isAxis", make_bool(control.isAxis));
    add(result, "value", make_number(static_cast<double>(control.value)));
    return result;
}

JsonValue hid_descriptor_json(const HidDescriptorView& view,
                              const std::vector<std::uint8_t>& raw) {
    JsonValue result = make_object();
    add(result, "parsed", make_bool(view.parsed));
    if (!view.error.empty())
        add(result, "error", make_string(view.error));
    add(result, "raw", bytes_json(raw));

    JsonValue elements = make_array();
    for (const hid::Element& element : view.descriptor.elements)
        elements.array.push_back(hid_element_json(element));
    add(result, "elements", std::move(elements));

    JsonValue reports = make_array();
    for (const hid::ReportInfo& report : view.descriptor.reports)
        reports.array.push_back(hid_report_info_json(report));
    add(result, "reports", std::move(reports));

    JsonValue collections = make_array();
    for (const hid::Collection& collection : view.descriptor.collections)
        collections.array.push_back(hid_collection_json(collection));
    add(result, "collections", std::move(collections));

    JsonValue controls = make_array();
    for (const HidControlView& control : view.controls)
        controls.array.push_back(hid_control_json(control));
    add(result, "genericLayout", std::move(controls));
    return result;
}
JsonValue hid_monitor_report_json(const HidMonitorReport& report) {
    JsonValue result = make_object();
    add(result, "sequence", make_number(static_cast<double>(report.sequence)));
    add(result, "type", make_string(hid_report_kind_name(report.kind)));
    add(result, "time", make_number(report.time));
    add(result, "data", bytes_json(report.data));
    return result;
}

JsonValue hid_error_json(const HidToolError& error) {
    JsonValue result = make_object();
    add(result, "code", make_number(static_cast<double>(error.code)));
    add(result, "deviceId", make_string(error.deviceId));
    add(result, "message", make_string(error.message));
    add(result, "time", make_number(error.time));
    return result;
}

bool required_bytes(const JsonValue& params, std::string_view name,
                    std::vector<std::uint8_t>& out, std::string& error) {
    const JsonValue* value = param(params, name);
    if (value == nullptr) {
        error = "missing parameter '" + std::string(name) + "'";
        return false;
    }
    if (!value->is_array()) {
        error = "parameter '" + std::string(name) + "' must be an array of bytes";
        return false;
    }
    out.clear();
    out.reserve(value->array.size());
    for (const JsonValue& item : value->array) {
        if (!item.is_number() || item.number < 0.0 || item.number > 255.0 ||
            item.number != std::floor(item.number)) {
            error = "parameter '" + std::string(name) + "' contains a non-byte value";
            return false;
        }
        out.push_back(static_cast<std::uint8_t>(item.number));
    }
    return true;
}

bool optional_bytes(const JsonValue& params, std::string_view name,
                    std::vector<std::uint8_t>& out, std::string& error) {
    if (param(params, name) == nullptr)
        return true;
    return required_bytes(params, name, out, error);
}

bool optional_uint8(const JsonValue& params, std::string_view name,
                    std::uint8_t fallback, std::uint8_t& out,
                    std::string& error) {
    const JsonValue* value = param(params, name);
    if (value == nullptr) {
        out = fallback;
        return true;
    }
    if (!value->is_number() || value->number < 0.0 || value->number > 255.0 ||
        value->number != std::floor(value->number)) {
        error = "parameter '" + std::string(name) + "' must be a byte";
        return false;
    }
    out = static_cast<std::uint8_t>(value->number);
    return true;
}

bool required_confirmed(const JsonValue& params, std::string& error) {
    const JsonValue* value = param(params, "confirmed");
    if (value == nullptr || !value->is_bool() || !value->boolean) {
        error = "write requires 'confirmed': true";
        return false;
    }
    return true;
}

bool optional_report_types(const JsonValue& params,
                           std::vector<backend::HidReportKind>& out,
                           std::string& error) {
    const JsonValue* value = param(params, "types");
    if (value == nullptr) {
        out = {backend::HidReportKind::Input};
        return true;
    }
    if (!value->is_array()) {
        error = "parameter 'types' must be an array";
        return false;
    }
    out.clear();
    for (const JsonValue& item : value->array) {
        if (!item.is_string()) {
            error = "parameter 'types' must contain strings";
            return false;
        }
        if (item.string == "input")
            out.push_back(backend::HidReportKind::Input);
        else if (item.string == "output")
            out.push_back(backend::HidReportKind::Output);
        else if (item.string == "feature")
            out.push_back(backend::HidReportKind::Feature);
        else {
            error = "unknown HID report type '" + item.string + "'";
            return false;
        }
    }
    return true;
}
std::optional<std::string> handle_request(const EditorSession& session,
                                          const EditorProtocolOptions& options,
                                          PreviewRuntime& preview,
                                          HidToolService& hidTools,
                                          const JsonValue& request) {
    const JsonValue* method = request.find("method");
    const JsonValue* id = request.find("id");
    const JsonValue* params = request.find("params");
    const bool notification = id == nullptr;
    const JsonValue responseId = id != nullptr ? *id : make_null();

    if (!request.is_object())
        return response_with_error(make_null(), kInvalidRequest, "request must be an object");
    if (method == nullptr || !method->is_string())
        return response_with_error(responseId, kInvalidRequest, "request has no method");
    if (id != nullptr && !(id->is_string() || id->is_number() || id->is_null()))
        return response_with_error(make_null(), kInvalidRequest, "request id has an invalid type");
    if (params != nullptr && !params->is_object())
        return response_with_error(responseId, kInvalidParams, "params must be an object");

    JsonValue empty = make_object();
    const JsonValue& arguments = params_or_empty(params, empty);

    try {
        if (method->string == "session.hello") {
            JsonValue result = make_object();
            add(result, "serverVersion", make_string(options.serverVersion));
            add(result, "protocolVersion", make_string(options.protocolVersion));
            add(result, "tokenValid", make_bool(true));
            JsonValue capabilities = make_array();
            capabilities.array.push_back(make_string("catalog.get"));
            capabilities.array.push_back(make_string("asset.open"));
            capabilities.array.push_back(make_string("asset.new"));
            capabilities.array.push_back(make_string("asset.validate"));
            capabilities.array.push_back(make_string("asset.save"));
            capabilities.array.push_back(make_string("asset.bake"));
            capabilities.array.push_back(make_string("catalog.controlPaths"));
            capabilities.array.push_back(make_string("preview"));
            capabilities.array.push_back(make_string("capture"));
            capabilities.array.push_back(make_string("hid"));
            add(result, "capabilities", std::move(capabilities));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "catalog.get") {
            static const EditorCatalog catalog = build_editor_catalog();
            return notification ? std::nullopt
                                : std::optional<std::string>(response_with_result(
                                      responseId, catalog_to_json(catalog)));
        }

        if (method->string == "catalog.controlPaths") {
            std::string error;
            std::string query;
            std::string expectedControlType;
            std::string deviceFilter;
            if (!optional_string(arguments, "query", query, error) ||
                !optional_string(arguments, "expectedControlType", expectedControlType,
                                error) ||
                !optional_string(arguments, "deviceFilter", deviceFilter, error)) {
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            }
            static const EditorCatalog catalog = build_editor_catalog();
            JsonValue result = make_object();
            add(result, "paths", control_paths_json(catalog, std::move(query),
                                                    std::move(expectedControlType),
                                                    std::move(deviceFilter)));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "asset.open") {
            std::string error;
            const std::string path = required_string(arguments, "path", error);
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            const EditorOpenResult opened = session.open(path);
            JsonValue result = make_object();
            add(result, "path", make_string(opened.path));
            add(result, "revision", make_string(opened.revision));
            add(result, "json", make_string(opened.json));
            add(result, "format", make_string(opened.format));
            add(result, "readOnly", make_bool(opened.readOnly));
            if (opened.schemaVersion > 0)
                add(result, "schemaVersion",
                    make_number(static_cast<double>(opened.schemaVersion)));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "asset.new") {
            std::string name;
            if (const JsonValue* value = param(arguments, "name")) {
                if (!value->is_string())
                    return notification ? std::nullopt
                                        : std::optional<std::string>(response_with_error(
                                              responseId, kInvalidParams,
                                              "parameter 'name' must be a string"));
                name = value->string;
            }
            JsonValue result = make_object();
            add(result, "json", make_string(session.new_asset(name)));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "asset.validate") {
            std::string error;
            const std::string json = required_string(arguments, "json", error);
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            JsonValue result = make_object();
            add(result, "diagnostics", diagnostics_json(session.validate(json)));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "asset.save") {
            std::string error;
            const std::string path = required_string(arguments, "path", error);
            if (error.empty()) {
                const std::string json = required_string(arguments, "json", error);
                if (error.empty()) {
                    std::string expectedRevision;
                    if (const JsonValue* value = param(arguments, "expectedRevision")) {
                        if (!value->is_string()) {
                            error = "parameter 'expectedRevision' must be a string";
                        } else {
                            expectedRevision = value->string;
                        }
                    }
                    bool createBackup = false;
                    if (error.empty() &&
                        !optional_bool(arguments, "createBackup", false, createBackup))
                        error = "parameter 'createBackup' must be a boolean";
                    if (error.empty()) {
                        const EditorSaveResult saved =
                            session.save(path, json, expectedRevision, createBackup);
                        JsonValue result = make_object();
                        add(result, "path", make_string(saved.path));
                        add(result, "revision", make_string(saved.revision));
                        add(result, "changed", make_bool(saved.changed));
                        return notification ? std::nullopt
                                            : std::optional<std::string>(
                                                  response_with_result(responseId,
                                                                       std::move(result)));
                    }
                }
            }
            return notification ? std::nullopt
                                : std::optional<std::string>(response_with_error(
                                      responseId, kInvalidParams, error));
        }

        if (method->string == "asset.bake") {
            std::string error;
            const std::string path = required_string(arguments, "path", error);
            if (error.empty()) {
                const std::string json = required_string(arguments, "json", error);
                if (error.empty()) {
                    std::string expectedRevision;
                    if (const JsonValue* value = param(arguments, "expectedRevision")) {
                        if (!value->is_string()) {
                            error = "parameter 'expectedRevision' must be a string";
                        } else {
                            expectedRevision = value->string;
                        }
                    }
                    bool createBackup = false;
                    if (error.empty() &&
                        !optional_bool(arguments, "createBackup", false, createBackup))
                        error = "parameter 'createBackup' must be a boolean";
                    if (error.empty()) {
                        const EditorBakeResult baked =
                            session.bake(path, json, expectedRevision, createBackup);
                        JsonValue result = make_object();
                        add(result, "path", make_string(baked.path));
                        add(result, "revision", make_string(baked.revision));
                        add(result, "bytes",
                            make_number(static_cast<double>(baked.bytes)));
                        add(result, "changed", make_bool(baked.changed));
                        return notification ? std::nullopt
                                            : std::optional<std::string>(
                                                  response_with_result(responseId,
                                                                       std::move(result)));
                    }
                }
            }
            return notification ? std::nullopt
                                : std::optional<std::string>(response_with_error(
                                      responseId, kInvalidParams, error));
        }

        if (method->string == "preview.start") {
            std::string json;
            bool hasJson = false;
            if (const JsonValue* value = param(arguments, "json")) {
                if (!value->is_string())
                    return notification ? std::nullopt
                                        : std::optional<std::string>(response_with_error(
                                              responseId, kInvalidParams,
                                              "parameter 'json' must be a string"));
                json = value->string;
                hasJson = true;
            }
            preview.start();
            std::uint64_t generation = preview.generation();
            if (hasJson) {
                try {
                    generation = preview.update_asset(json, 0);
                } catch (...) {
                    preview.stop();
                    throw;
                }
            }
            JsonValue result = make_object();
            add(result, "generation", make_number(static_cast<double>(generation)));
            add(result, "running", make_bool(preview.running()));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "preview.update") {
            std::string error;
            const std::string json = required_string(arguments, "json", error);
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            std::uint64_t requestedGeneration = 0;
            if (!optional_generation(arguments, requestedGeneration, error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            const std::uint64_t generation =
                preview.update_asset(json, requestedGeneration);
            JsonValue result = make_object();
            add(result, "generation", make_number(static_cast<double>(generation)));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "preview.stop") {
            preview.stop();
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "preview.snapshot") {
            return notification ? std::nullopt
                                : std::optional<std::string>(response_with_result(
                                      responseId, preview.snapshot()));
        }

        if (method->string == "preview.input.key") {
            std::string error;
            const std::string code = required_string(arguments, "code", error);
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            bool pressed = true;
            bool repeat = false;
            if (!optional_bool(arguments, "pressed", true, pressed) ||
                !optional_bool(arguments, "repeat", false, repeat))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams,
                                          "parameter 'pressed' and 'repeat' must be booleans"));
            const bool recognized = preview.input_key(code, pressed, repeat);
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            add(result, "recognized", make_bool(recognized));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "preview.input.mouse") {
            std::string error;
            PreviewMouseInput input;
            double value = 0.0;
            if (!optional_number(arguments, "x", 0.0, value, error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            input.x = static_cast<float>(value);
            input.hasX = param(arguments, "x") != nullptr;
            if (!optional_number(arguments, "y", 0.0, value, error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            input.y = static_cast<float>(value);
            input.hasY = param(arguments, "y") != nullptr;
            if (const JsonValue* button = param(arguments, "button")) {
                if (!button->is_number() || button->number < 0.0 ||
                    button->number > 4.0 || button->number != std::floor(button->number))
                    return notification ? std::nullopt
                                        : std::optional<std::string>(response_with_error(
                                              responseId, kInvalidParams,
                                              "parameter 'button' must be an integer from 0 to 4"));
                input.hasButton = true;
                input.button = static_cast<int>(button->number);
                if (!optional_bool(arguments, "pressed", false, input.pressed))
                    return notification ? std::nullopt
                                        : std::optional<std::string>(response_with_error(
                                              responseId, kInvalidParams,
                                              "parameter 'pressed' must be a boolean"));
            }
            if (!optional_number(arguments, "wheelX", 0.0, value, error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            input.wheelX = static_cast<float>(value);
            if (!optional_number(arguments, "wheelY", 0.0, value, error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            input.wheelY = static_cast<float>(value);
            if (!optional_number(arguments, "deltaX", 0.0, value, error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            input.deltaX = static_cast<float>(value);
            if (!optional_number(arguments, "deltaY", 0.0, value, error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            input.deltaY = static_cast<float>(value);
            input.hasWheel = param(arguments, "wheelX") != nullptr ||
                             param(arguments, "wheelY") != nullptr;
            input.hasDelta = param(arguments, "deltaX") != nullptr ||
                             param(arguments, "deltaY") != nullptr;
            const bool changed = preview.input_mouse(input);
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            add(result, "changed", make_bool(changed));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "capture.start") {
            std::string error;
            std::string expectedControlType;
            std::string deviceFilter;
            if (!optional_string(arguments, "expectedControlType", expectedControlType,
                                 error) ||
                !optional_string(arguments, "deviceFilter", deviceFilter, error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            preview.start();
            preview.start_capture(std::move(expectedControlType),
                                     std::move(deviceFilter));
            JsonValue result = make_object();
            add(result, "captureId", make_string("capture"));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "capture.stop") {
            preview.stop_capture();
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.devices") {
            JsonValue result = make_array();
            for (const backend::DeviceView& view : hidTools.devices())
                result.array.push_back(hid_device_json(view));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.descriptor") {
            std::string error;
            const std::string deviceId = required_string(arguments, "deviceId", error);
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            std::string descriptorError;
            const std::optional<HidDescriptorView> view =
                hidTools.descriptor(deviceId, descriptorError);
            if (!view.has_value())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, descriptorError));
            const std::optional<backend::DeviceView> device =
                hidTools.device(deviceId);
            const std::vector<std::uint8_t> raw =
                device.has_value() ? device->info.reportDescriptor
                                   : std::vector<std::uint8_t>{};
            JsonValue result = hid_descriptor_json(*view, raw);
            add(result, "device", device.has_value()
                                      ? hid_device_json(*device)
                                      : make_object());
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.reports.start") {
            std::string error;
            const std::string deviceId = required_string(arguments, "deviceId", error);
            std::vector<backend::HidReportKind> types;
            if (error.empty()) { optional_report_types(arguments, types, error); }
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            const std::string monitorId =
                hidTools.start_monitor(deviceId, std::move(types), error);
            if (monitorId.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            JsonValue result = make_object();
            add(result, "monitorId", make_string(monitorId));
            add(result, "deviceId", make_string(deviceId));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.reports.poll") {
            std::string error;
            const std::string monitorId = required_string(arguments, "monitorId", error);
            double maxValue = 200.0;
            if (error.empty()) { optional_number(arguments, "max", 200.0, maxValue, error); }
            if (!error.empty() || maxValue < 0.0 || maxValue > 5000.0 ||
                maxValue != std::floor(maxValue))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams,
                                          error.empty() ? "parameter 'max' is out of range"
                                                        : error));
            if (!hidTools.has_monitor(monitorId))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kNotFound,
                                          "HID monitor not found: " + monitorId));
            std::size_t dropped = 0;
            bool paused = false;
            const std::vector<HidMonitorReport> reports =
                hidTools.poll_monitor(monitorId,
                                      static_cast<std::size_t>(maxValue), dropped,
                                      paused);
            JsonValue result = make_object();
            JsonValue list = make_array();
            for (const HidMonitorReport& report : reports)
                list.array.push_back(hid_monitor_report_json(report));
            add(result, "reports", std::move(list));
            add(result, "dropped", make_number(static_cast<double>(dropped)));
            add(result, "paused", make_bool(paused));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.reports.pause") {
            std::string error;
            const std::string monitorId = required_string(arguments, "monitorId", error);
            bool paused = false;
            if (error.empty() &&
                !optional_bool(arguments, "paused", true, paused))
                error = "parameter 'paused' must be a boolean";
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            if (!hidTools.pause_monitor(monitorId, paused))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kNotFound,
                                          "HID monitor not found: " + monitorId));
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            add(result, "paused", make_bool(paused));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.reports.clear") {
            std::string error;
            const std::string monitorId = required_string(arguments, "monitorId", error);
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            if (!hidTools.clear_monitor(monitorId))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kNotFound,
                                          "HID monitor not found: " + monitorId));
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.reports.stop") {
            std::string error;
            const std::string monitorId = required_string(arguments, "monitorId", error);
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            if (!hidTools.stop_monitor(monitorId))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kNotFound,
                                          "HID monitor not found: " + monitorId));
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.feature.read") {
            std::string error;
            const std::string deviceId = required_string(arguments, "deviceId", error);
            std::uint8_t reportId = 0;
            double sizeValue = 0.0;
            if (error.empty())
                optional_uint8(arguments, "reportId", 0, reportId, error);
            if (error.empty()) { optional_number(arguments, "size", 0.0, sizeValue, error); }
            if (!error.empty() || sizeValue < 1.0 || sizeValue > 4096.0 ||
                sizeValue != std::floor(sizeValue))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams,
                                          error.empty() ? "parameter 'size' must be an integer from 1 to 4096"
                                                        : error));
            std::vector<std::uint8_t> bytes;
            if (!hidTools.read_feature(deviceId, reportId,
                                       static_cast<std::size_t>(sizeValue), bytes,
                                       error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kIoError, error));
            JsonValue result = make_object();
            add(result, "bytes", bytes_json(bytes));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.feature.write" ||
            method->string == "hid.output.write") {
            std::string error;
            const std::string deviceId = required_string(arguments, "deviceId", error);
            std::vector<std::uint8_t> bytes;
            if (error.empty()) { required_bytes(arguments, "bytes", bytes, error); }
            if (error.empty()) { required_confirmed(arguments, error); }
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            const bool feature = method->string == "hid.feature.write";
            const bool ok = feature ? hidTools.write_feature(deviceId, bytes, error)
                                    : hidTools.write_output(deviceId, bytes, error);
            if (!ok)
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kIoError, error));
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            add(result, "bytes", make_number(static_cast<double>(bytes.size())));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.haptics") {
            std::string error;
            const std::string deviceId = required_string(arguments, "deviceId", error);
            double left = 0.0;
            double right = 0.0;
            if (error.empty()) { optional_number(arguments, "left", 0.0, left, error); }
            if (error.empty()) { optional_number(arguments, "right", 0.0, right, error); }
            if (error.empty()) { required_confirmed(arguments, error); }
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            if (!hidTools.haptics(deviceId, static_cast<float>(left),
                                  static_cast<float>(right), error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kIoError, error));
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.lightbar") {
            std::string error;
            const std::string deviceId = required_string(arguments, "deviceId", error);
            double red = 0.0;
            double green = 0.0;
            double blue = 0.0;
            if (error.empty()) { optional_number(arguments, "r", 0.0, red, error); }
            if (error.empty()) { optional_number(arguments, "g", 0.0, green, error); }
            if (error.empty()) { optional_number(arguments, "b", 0.0, blue, error); }
            if (error.empty()) { required_confirmed(arguments, error); }
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            if (!hidTools.lightbar(deviceId, static_cast<float>(red),
                                   static_cast<float>(green),
                                   static_cast<float>(blue), error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kIoError, error));
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.adaptiveTrigger") {
            std::string error;
            const std::string deviceId = required_string(arguments, "deviceId", error);
            std::string side = "right";
            if (error.empty()) { optional_string(arguments, "side", side, error); }
            if (side != "left" && side != "right")
                error = "parameter 'side' must be 'left' or 'right'";
            std::uint8_t mode = 0;
            if (error.empty())
                optional_uint8(arguments, "mode", 0, mode, error);
            std::vector<std::uint8_t> parameters;
            if (error.empty())
                optional_bytes(arguments, "params", parameters, error);
            if (error.empty()) { required_confirmed(arguments, error); }
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            if (!hidTools.adaptive_trigger(deviceId, side == "left", mode,
                                           parameters, error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kIoError, error));
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.switch.subcommand") {
            std::string error;
            const std::string deviceId = required_string(arguments, "deviceId", error);
            std::uint8_t subcommand = 0;
            if (error.empty())
                optional_uint8(arguments, "subcommand", 0, subcommand, error);
            std::vector<std::uint8_t> payload;
            if (error.empty())
                optional_bytes(arguments, "payload", payload, error);
            if (error.empty()) { required_confirmed(arguments, error); }
            if (!error.empty())
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kInvalidParams, error));
            if (!hidTools.switch_subcommand(deviceId, subcommand, payload, error))
                return notification ? std::nullopt
                                    : std::optional<std::string>(response_with_error(
                                          responseId, kIoError, error));
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.errors") {
            JsonValue result = make_object();
            JsonValue errors = make_array();
            for (const HidToolError& error : hidTools.errors())
                errors.array.push_back(hid_error_json(error));
            add(result, "errors", std::move(errors));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        if (method->string == "hid.errors.clear") {
            hidTools.clear_errors();
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }
        if (method->string == "app.shutdown") {
            if (options.requestShutdown)
                options.requestShutdown();
            JsonValue result = make_object();
            add(result, "ok", make_bool(true));
            return notification ? std::nullopt
                                : std::optional<std::string>(
                                      response_with_result(responseId, std::move(result)));
        }

        return notification ? std::nullopt
                            : std::optional<std::string>(response_with_error(
                                  responseId, kMethodNotFound,
                                  "method not found: " + method->string));
    } catch (const EditorError& error) {
        return notification ? std::nullopt
                            : std::optional<std::string>(response_with_error(
                                  responseId, error_code_for(error), error.what(),
                                  error.diagnostics()));
    } catch (const asset::AssetError& error) {
        std::vector<EditorDiagnostic> diagnostics;
        diagnostics.push_back({EditorDiagnostic::Severity::Error, "asset.parse",
                               error.what(), {}, error.line(), error.column()});
        return notification ? std::nullopt
                            : std::optional<std::string>(response_with_error(
                                  responseId, kValidationFailed, "asset validation failed",
                                  diagnostics));
    } catch (const std::exception& error) {
        return notification ? std::nullopt
                            : std::optional<std::string>(response_with_error(
                                  responseId, kInternalError, error.what()));
    }
}

} // namespace

EditorProtocol::EditorProtocol(const EditorSession& session,
                               EditorProtocolOptions options)
    : m_session(session), m_options(std::move(options)) {
    PreviewRuntimeOptions previewOptions;
    previewOptions.enableHid = m_options.enablePreviewHid;
    m_preview = std::make_unique<PreviewRuntime>(std::move(previewOptions));
    m_hidTools = std::make_unique<HidToolService>(*m_preview);
}

EditorProtocol::~EditorProtocol() = default;

std::optional<std::string> EditorProtocol::handle(std::string_view message) const {
    if (message.size() > m_options.maxMessageBytes)
        return response_with_error(make_null(), kRequestTooLarge,
                                   "request exceeds maximum message size");

    JsonValue request;
    asset::JsonParseError parseError;
    if (!asset::parse_json(message, request, parseError)) {
        return response_with_error(make_null(), kParseError,
                                   parseError.message + " at line " +
                                       std::to_string(parseError.line) + ", column " +
                                       std::to_string(parseError.column));
    }

    const JsonValue* jsonrpc = request.find("jsonrpc");
    if (!request.is_object() || jsonrpc == nullptr || !jsonrpc->is_string() ||
        jsonrpc->string != "2.0") {
        const JsonValue* id = request.find("id");
        return response_with_error(id != nullptr ? *id : make_null(), kInvalidRequest,
                                   "invalid JSON-RPC request");
    }

    return handle_request(m_session, m_options, *m_preview, *m_hidTools, request);
}

} // namespace insys::editor
