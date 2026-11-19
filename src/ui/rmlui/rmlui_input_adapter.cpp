#include "insys/ui/rmlui_input_adapter.h"

namespace insys::ui {

RmlUiInputAdapter::RmlUiInputAdapter(Rml::Context& context, Manager& manager)
    : UiInputAdapter(nullptr, manager), m_sink(context) {
    set_sink(&m_sink);
}

void RmlUiInputAdapter::set_context(Rml::Context& context) {
    m_sink.set_context(&context);
    set_sink(&m_sink);
}

} // namespace insys::ui
