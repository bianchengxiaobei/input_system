# insys - Standalone C++ Input System

A standalone C++20 input system and web-based input actions asset editor. The
core library (`insys`) has no dependency on GLFW, RmlUi, or OpenGL. The GLFW
backend and the RmlUi bridge live in separate static libraries, and the demo
links all of them.

This repository contains only the C++ implementation, editor, tests and
documentation. It does not include the Unity Input System package or the Unity
upstream repository history. Architecture and milestone documentation starts at
[`Docs/CppInputSystem/README.md`](Docs/CppInputSystem/README.md).

## Requirements

- CMake 3.21+ and a C++20 compiler
- Windows: Visual Studio 2022 (MSVC), vcpkg with `glfw3`, `rmlui` 6.3 and
  `catch2`, plus the Edge WebView2 Runtime (the SDK is downloaded by CMake).
  Set `VCPKG_ROOT` to the vcpkg checkout (for example `E:\Dev\vcpkg`).
- macOS: Xcode command-line tools with the system WebKit framework
- Linux: GTK 3 and WebKitGTK development packages, for example
  `libgtk-3-dev` and `libwebkit2gtk-4.1-dev` (`webkit2gtk-4.0` is also accepted)
- Node.js 20+ and npm only for the optional editor web frontend
- Playwright Chromium for `npm run test:e2e` (`npx playwright install chromium`)

## Build

```powershell
$env:VCPKG_ROOT = "C:\src\vcpkg"
cmake --preset default
cmake --build --preset debug
ctest --preset debug
```

Current test counts: `insys_tests` 288 cases / 3500 assertions; `insys_ui_tests`
15 cases / 133 assertions; `insys_editor_tests` 28 cases / 375 assertions (the
editor tests are built when `INSYS_BUILD_EDITOR=ON`); frontend `npm test` 24
cases and Playwright `npm run test:e2e` 7 cases (6 browser + 1 WebView2).
`npm audit` currently reports 0 vulnerabilities.

The editor server is opt-in:

```powershell
cmake --preset editor
cmake --build --preset editor-debug
ctest --preset editor-debug
cd editor/web
npm install
npm test
npm run build
```

The editor target also builds `insys_editor_server` without GLFW or RmlUi, plus
the native `insys_editor` shell: WebView2 on Windows, WKWebView on macOS and
WebKitGTK on Linux. `INSYS_BUILD_EDITOR_WEB=ON` additionally runs `npm run
build` before the server target; otherwise build the frontend manually as shown
above. The shell copies `editor/web/dist` to its output `web/` directory (or
`Contents/Resources/web` inside the macOS app bundle).

The WebView2 native SDK is pinned to `Microsoft.Web.WebView2 1.0.3537.50` and
downloaded into the build tree by `cmake/WebView2.cmake`. For offline builds set
`INSYS_WEBVIEW2_ROOT` to an extracted NuGet package, or configure with
`INSYS_BUILD_EDITOR_SHELL=OFF`.

On macOS or Linux use the Unix preset (the Windows presets use Visual Studio):

```bash
cd editor/web && npm install && npm run build && cd ../..
cmake --preset editor-unix
cmake --build --preset editor-unix-debug
```

Linux enables the shell only when `webkit2gtk-4.1` or `webkit2gtk-4.0` and
GTK 3 are available through `pkg-config`. The macOS app is written to
`build-editor-unix/insys_editor.app`; the Linux executable is
`build-editor-unix/insys_editor`. These platform shells are implemented in
`tools/editor_shell/main_macos.mm` and `main_linux.cpp`; macOS/Linux smoke
validation is still pending.

Binary action assets can be baked from JSON with `asset_bake`:

```powershell
.\build\tools\Debug\asset_bake.exe .\samples\acceptance\assets\player.json .\player.insysb
```

`AssetLoader::load_from_file` selects the JSON or `.insysb` reader from the file
extension; `--benchmark` on `asset_bake` prints JSON/binary load timing.

The demo is written to `build/samples/acceptance/Debug/insys_demo.exe` (the
assets folder and required DLLs are copied next to it).

## Run the demo

```powershell
.\build\samples\acceptance\Debug\insys_demo.exe
```

Expected behaviour:

- A GLFW window opens with an RmlUi panel.
- Clicking **Click me** prints `clicked (n)` and updates the counter.
- Key presses/releases and mouse clicks are logged to the console.
- Typing into the text field inserts characters and prints `[text] ...`.
- The panel lists all devices; connected gamepads show stick/trigger/button state.
- Move/Jump/Fire actions display phase, value and the driving control.
- Rebind Jump or Fire, save overrides, and reset them; overrides are loaded on
  the next launch from `assets/bindings.json`.
- M4 adds Devices/Actions/UI Input/Rebinding tabs, text input, UI gating, wheel
  scrolling and gamepad navigation. **Toggle navigation source** switches between
  raw gamepad controls and the `UI/Navigate`, `UI/Submit`, `UI/Cancel` actions
  from `assets/ui.json`.
- M5 adds Debugger and Trace pages. The Debugger page shows devices/controls, action phases, recent events and bindings; the Trace page records, saves, loads, steps and replays low-level events.
- Use `--page N` to open a page directly (0 Devices, 1 Actions, 2 UI Input, 3 Rebinding, 4 Debugger, 5 Trace); `--trace` starts recording immediately.
- Use `--smoke N` to render N frames and exit (prints a `[smoke]` status line).
- Windows: `--raw-input` enables Win32 Raw Input deltas (extra physical mice become
  separate `RawInput` devices); `--xinput` enables the 4-slot XInput backend and
  suppresses duplicate GLFW Xbox gamepads; `--hid` enables the HID backend for
  raw DualShock/DualSense/Switch/Steam and generic HID controllers.
- macOS/Linux: `--hid` enables the IOHIDManager/hidraw HID backend on the same
  controller families.

## Run the editor

```powershell
.\build-editor\Debug\insys_editor.exe --asset-root .\samples\acceptance\assets
```

`insys_editor.exe` starts the embedded server on a random loopback port, opens a
WebView2 window, and serves the copied `web/` assets. It restricts navigation to
the local editor origin, blocks popups, and disables DevTools by default. If
WebView2 is unavailable it opens the same URL in an external browser and keeps
the local server alive until the window closes. On macOS and Linux the same
shell uses WKWebView and WebKitGTK respectively. Useful flags:
`--browser-only`, `--open-browser`, `--browser PATH`, `--devtools`,
`--url-file PATH`, `--log PATH`, `--user-data-dir PATH`, and
`--remote-debugging-port N` for local E2E on Windows.

On macOS and Linux run `build-editor-unix/insys_editor.app` or
`build-editor-unix/insys_editor` with the same flags; build the frontend before the
shell so the post-build copy contains `web/`.

Use `File > Open Asset...` (Ctrl+O / Cmd+O) to pick a `.json` or `.insysb` with
a native file dialog. Use `--open-browser` to open the editor in a separate
browser tab as well as the native web view; `--browser PATH` selects a specific
browser executable. `File > Open in Browser` (Ctrl+Shift+B / Cmd+Shift+B) does
the same from the running window, while `--browser-only` skips the web view
entirely. The shell window owns the local server, so closing it also closes the
external editor session. The URL contains the session token and should not be
shared.

```powershell
# WebView2 plus Chrome
.\build-editor\Debug\insys_editor.exe --asset-root .\samples\acceptance\assets `
  --open-browser --browser "C:\Program Files\Google\Chrome\Application\chrome.exe"

# External browser only
.\build-editor\Debug\insys_editor.exe --asset-root .\samples\acceptance\assets `
  --browser-only --browser "C:\Program Files\Google\Chrome\Application\chrome.exe"

# Portable Windows package
cmake --build --preset editor-release --target insys_editor
cpack --config build-editor\CPackConfig.cmake -C Release

# MSIX (unsigned by default)
cmake --build --preset editor-release --target insys_editor_msix
```

The portable package is written to
`build-editor\package\insys-editor-0.1.0-windows-x64.zip` with a `.sha256`
checksum. It bundles the MSVC runtime but still requires the Edge WebView2
Runtime. The MSIX is written to
`build-editor\package\insys-editor-0.1.0-windows-x64.msix` with a `.sha256`
checksum.

To create and install a development MSIX, generate and trust a self-signed
certificate. The root store import needs an elevated shell:

```powershell
cmake --preset editor -DINSYS_MSIX_SELF_SIGN=ON
cmake --build --preset editor-release --target insys_editor_msix

# elevated shell for the root store
certutil -addstore -f Root build-editor\package\insys-editor-0.1.0-windows-x64-dev.cer
Import-Certificate -FilePath build-editor\package\insys-editor-0.1.0-windows-x64-dev.cer `
  -CertStoreLocation Cert:\CurrentUser\TrustedPeople
Add-AppxPackage -Path build-editor\package\insys-editor-0.1.0-windows-x64.msix

$pfn = (Get-AppxPackage -Name insys.editor).PackageFamilyName
Start-Process "shell:AppsFolder\$pfn!insys.editor"
```

Installing the root only in `TrustedPeople` is not enough; Windows reports
`0x800B0109` until the self-signed root is trusted. The generated certificate
subject must match the manifest publisher (`CN=insys` by default). Production
packages can set `-DINSYS_MSIX_PUBLISHER`, `-DINSYS_MSIX_CERTIFICATE` and
`-DINSYS_MSIX_TIMESTAMP_URL`; set the PFX password in the
`INSYS_MSIX_CERTIFICATE_PASSWORD` environment variable. A Store identity is not
configured yet. The development package was installed, launched from the
package identity, and uninstalled successfully during validation.

The standalone server remains available for development:

```powershell
.\build-editor\Debug\insys_editor_server.exe --asset-root .\samples\acceptance\assets
```

The server binds to `127.0.0.1`, prints a one-time token URL, and serves
`editor/web/dist` in production mode. Open the printed URL in a browser. The
frontend supports structured ActionMap/Action/Binding/Composite/ControlScheme
editing, catalog-backed pickers, undo/redo, copy/paste, semantic diagnostics,
raw JSON apply/format, revision-checked saves with `.bak` backups, and a
Preview tab with 150 ms hot reload, keyboard/mouse drive input, device/action/
event snapshots, Listen capture and derived bindings. The HID Tools tab adds device/descriptor inspection, generic layout mapping, raw input/output/feature report monitoring with JSONL export, feature/output writes, and rumble/lightbar/adaptive-trigger/Switch test panels; every write requires an explicit confirmation.

E4 adds read-only `.insysb` import with Save As JSON, `asset.bake` export,
schema-version diagnostics, stable JSON ordering, recent-file and panel-layout
persistence, a configurable preview poll rate, and a 34 px virtualized action
tree that keeps 1000+ bindings responsive. The native shell embeds the server
and can run without manual browser launch. Playwright covers the
editing/save/bake/conflict/preview flows and attaches to the real WebView2
shell through CDP. Closing with unsaved edits prompts Save, Discard or Cancel
(browser mode uses `beforeunload`). The optional **Auto save** toolbar toggle
saves on focus loss and before the WebView2 shell closes. The shell also has a
native `File > Open Asset...` dialog (Ctrl+O) and can open the same editor in
an external browser via the File menu or `--open-browser`, with an optional
`--browser PATH`. Windows packaging supports both a portable ZIP (CPack) and an
MSIX package with optional self-signed development certificate. macOS and Linux
shells use WKWebView and WebKitGTK through the same frontend host API; their
compile and smoke validation is still outstanding because this workspace is
Windows-only.

Playwright E2E (after building the editor targets and frontend):

```powershell
cd editor\web
npx playwright install chromium
npm run test:e2e
```

`e2e/global-setup.ts` starts a temporary `insys_editor_server.exe` on port 0
with a temporary asset root and cleans it up after the run. On Windows the
suite also launches `insys_editor.exe` with an ephemeral CDP port.

For frontend development:

```powershell
.\build-editor\Debug\insys_editor_server.exe --port 17800 --asset-root .\samples\acceptance\assets
cd editor\web
npm run dev
```

Open `http://localhost:5173/?token=<token>`; Vite proxies `/ws` to port 17800.
Use `--static PATH` to point the production server at a different frontend
build, and `--allow-origin URL` to allow an additional loopback dev origin.

## Layout

```
include/insys/        public headers (core, backend interfaces, RmlUi bridge, editor)
src/core/             event/state/layout/device/manager, listeners, trace and debugger model
src/actions/          actions, binding resolver, processors, composites, interactions, rebinding
src/asset/            JSON parser and action asset loader/serializer
src/backend/          neutral runtime, GLFW backend, Win32 Raw Input/HID, XInput and HID report backend
src/editor/           editor session, JSON-RPC protocol, HTTP/WebSocket server
src/hid/              HID descriptor parser, generic layout builder and product report codecs
src/ui/               UI event sink, generic pointer module, input adapter and RmlUi bridge
src/onscreen/         virtual-device OnScreen controls
cmake/WebView2.cmake  pinned WebView2 SDK download and static loader lookup
tools/editor/         insys_editor_server executable
tools/editor_shell/   native insys_editor launcher (WebView2/WKWebView/WebKitGTK)
editor/web/           React/Vite editor frontend
editor/web/e2e/       Playwright browser and WebView2 E2E tests
Docs/                 architecture, milestone and implementation docs
tests/                Catch2 unit tests (core, UI and editor targets)
samples/acceptance/   demo (GLFW + RmlUi + official Renderer_GL3)
```

## Notes

- `samples/acceptance/vendor/rmlui/` contains the RmlUi 6.3 `Renderer_GL3`
  sources copied from the RmlUi repository (MIT). It is used only by the demo;
  the input backend is ours, not RmlUi's GLFW backend.
- M1 adds the layout registry, `DeviceBuilder`, `ControlPath`, gamepad/joystick
  layouts and GLFW gamepad polling/hotplug.
- M2 adds actions, binding resolution, processors, composites, action phases,
  initial state checks and conflict resolution.
- M3 adds Tap/Hold/SlowTap/Press/MultiTap interactions, timeout integration,
  interactive rebinding, binding override save/load, JSON action assets and
  simplified control schemes. The high-level player layer adds `InputUser` and
  `PlayerInput` with per-player cloned actions, device pairing/unpairing,
  control-scheme auto matching, join/leave and action-map switching.
- M4 adds RmlUi 6.3 input integration: pointer/keyboard/text/wheel translation,
  capture policies, raw and action-driven navigation, high-DPI coordinate
  scaling and a multi-page acceptance demo. IME composition is not implemented;
  direct UTF-32 text input is supported.
- M6 adds opt-in Windows Raw Input and XInput backends. Raw Input provides
  unaccelerated mouse deltas and per-device mouse streams; XInput provides
  normalized Xbox gamepad state, 4-slot hotplug and dual-motor haptics through
  `SendHapticImpulseCommand`. M6.3 adds screen-orientation compensation
  processors, `OneModifier`/`TwoModifiers` shortcut composites, optional
  piecewise-linear deadzone curves, time-based smoothing processors and basic
  touch gestures. M6.4 adds portable Linux evdev mapping/state aggregation,
  event/capability decoding, capture sessions, hotplug tracking, Linux
  `/dev/input` sources, inotify decoding/watcher, backend integration and macOS
  GameController mapping/backend layers. M6.5 adds binary
  `.insysb` assets with a `asset_bake` tool. M6.6 core adds a ten-touch
  `Touchscreen` and Gyroscope/Accelerometer/Gravity/LinearAcceleration/MagneticField/Attitude,
  Light/Pressure/Proximity/Humidity/AmbientTemperature/StepCounter/HingeAngle,
  Pen and TrackedDevice devices, plus
  Windows `WM_TOUCH` capture, sensor backend integration, a portable touch
  backend, a mobile input bridge, Android sensor event mapping, iOS CoreMotion
  mapping/platform callbacks, Android JNI entry points, Android Java
  SensorEventListener/MotionEvent adapters, iOS touch callbacks, iOS CMPedometer
  step counter support and a cross-thread event queue; other platform capture backends remain optional. M6.7 core adds
  device/event mirroring with a binary remoting protocol,
  `LoopbackTransport`, IPv4 TCP/UDP transports and a WebSocket transport; TLS
  and the other M6 options remain optional. EnhancedTouch adds `Touch`/`Finger`/
  `TouchHistory`/`TouchSimulation`; OnScreen adds a generic pointer input module,
  virtual-device-backed `OnScreenButton`/`OnScreenStick` and pointer target
  dispatch for UI widgets. Device state is preserved across hotplug buffer
  rebuilds. M7 adds a HID report descriptor parser, generic descriptor-driven
  layouts with Gamepad/Joystick semantic mapping, `HidGamepad`/DualShock3/
  DualShock4/DualSense/Switch Pro/Steam/Steam Deck layouts, DS3/DS4/DualSense/
  Switch/Steam/Steam Deck report codecs, touchpad and IMU decoding, product
  haptic encoders, DualSense adaptive-trigger presets, Bluetooth input/output
  CRC, Switch subcommands, DS3 rumble and Steam Deck feature-report haptics,
  plus a platform-driven `HidBackend` with merged multi-report devices,
  dynamic report-id routing, raw output/feature report commands, feature
  report reads and `HidError` diagnostics. Windows/Linux/macOS HID
  enumeration/read/write is wired through `WindowsHid`, `LinuxHid` and
  `MacOsHid`, including transport detection, serial deduplication, bounded
  report queues and write retries plus the `--hid` demo switch. Steam Input API
  integration remains external platform work; macOS IOHID still needs
  compile/runtime validation. M8 adds the first editor slice: `insys_hid_platform`
  is independent of GLFW, the `insys_editor_server` exposes token-protected
  JSON-RPC over WebSocket and static assets on loopback, `asset.open`,
  `asset.new`, `asset.validate` and `asset.save` are implemented with SHA-256
  revisions, atomic replace and backups, JSON action `priority` round-trips, and
  the React E2 editor provides the three-pane layout, catalog-backed structured
  ActionMap/Action/Binding/Composite/ControlScheme editing, undo/redo,
  copy/paste, raw JSON switching, diagnostics and a runtime preview with
  browser key/mouse injection, HID devices, Listen capture, hot reload and HID descriptor/report tools with confirmed feature/output writes.
