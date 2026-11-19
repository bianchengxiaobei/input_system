#include "insys/system.h"

namespace insys {

System::System(IRuntime& runtime, const Settings& settings)
    : m_manager(std::make_unique<Manager>(runtime, settings)) {
    runtime.set_manager(m_manager.get());
}

System::~System() = default;

void System::update(UpdateType type) {
    m_manager->update(type);
}

} // namespace insys
