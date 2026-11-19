#pragma once

namespace insys {

class ITime {
public:
    virtual ~ITime() = default;
    virtual double now() const = 0;
};

} // namespace insys
