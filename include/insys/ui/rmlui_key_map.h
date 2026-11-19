#pragma once

#include <RmlUi/Core/Input.h>

#include "insys/low/key.h"

namespace insys::ui {

/// Maps an insys key to the RmlUi 6.x key identifier.
/// Returns Rml::Input::KI_UNKNOWN for keys that have no UI meaning.
Rml::Input::KeyIdentifier map_key_to_rmlui(Key key);

} // namespace insys::ui
