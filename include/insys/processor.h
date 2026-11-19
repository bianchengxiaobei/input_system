#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "insys/actions/value_type.h"
#include "insys/low/primitive_value.h"

namespace insys {

class Control;
class IRuntime;
struct Settings;

struct ProcessorContext {
    const Settings* settings = nullptr;
    const IRuntime* runtime = nullptr;
};

class IProcessor {
public:
    enum class CachingPolicy {
        CacheResult,
        EvaluateOnEveryRead,
    };

    virtual ~IProcessor() = default;

    virtual ValueType value_type() const = 0;
    virtual void process(void* value, std::size_t size, const Control* control) const = 0;
    virtual void process(void* value, std::size_t size, const Control* control,
                         const ProcessorContext& context) const {
        (void)context;
        process(value, size, control);
    }
    virtual CachingPolicy caching_policy() const { return CachingPolicy::CacheResult; }
    virtual bool set_parameter(std::string_view name, const PrimitiveValue& value) {
        (void)name;
        (void)value;
        return false;
    }
};

} // namespace insys
