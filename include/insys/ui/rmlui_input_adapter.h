#pragma once

#include "insys/ui/rmlui_event_sink.h"
#include "insys/ui/ui_input_adapter.h"

namespace insys::ui {

/// RmlUi-specific UiInputAdapter. Lifetime note: the RmlUi context must
/// outlive this object.
class RmlUiInputAdapter : public UiInputAdapter {
public:
    RmlUiInputAdapter(Rml::Context& context, Manager& manager);

    void set_context(Rml::Context& context);

private:
    RmlUiEventSink m_sink;
};

} // namespace insys::ui
