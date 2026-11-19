#include "insys/low/fourcc.h"

namespace insys {

std::string to_string(FourCC code) {
    std::string result(4, ' ');
    result[0] = static_cast<char>((code.value >> 24) & 0xFF);
    result[1] = static_cast<char>((code.value >> 16) & 0xFF);
    result[2] = static_cast<char>((code.value >> 8) & 0xFF);
    result[3] = static_cast<char>(code.value & 0xFF);
    return result;
}

} // namespace insys
