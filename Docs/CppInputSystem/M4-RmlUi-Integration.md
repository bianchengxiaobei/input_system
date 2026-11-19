# M4 —— RmlUi 输入集成

> 前置：M1（指针/键盘/文本）；手柄导航依赖 M2/M3。
> 目标：把本库的输入翻译为 RmlUi 的指针/键盘/文本/导航事件；实现 SystemInterface；实现 UI 焦点与消费门控；完成验收 Demo。
> 参考：`../CppInputSystem_ImplementationPlan.md` 第 12、14 章。

---

## 4.1 目标与交付物

- `RmlUiSystemInterface`：时间、光标、剪贴板、日志、系统度量。
- `RmlUiInputAdapter`：鼠标、键盘、文本、滚轮、导航。
- 焦点/消费策略：Full / PointerAndText / None。
- Action 驱动的 UI 导航（可选）。
- 文本输入（ASCII/直接 UTF-32；IME 可选）。
- Demo 完整验收。

---

## 4.2 任务清单

- [x] 4.2.1 确认 RmlUi 版本与 API
- [x] 4.2.2 RmlUiSystemInterface
- [x] 4.2.3 鼠标映射
- [x] 4.2.4 键盘映射
- [x] 4.2.5 文本输入
- [x] 4.2.6 滚轮
- [x] 4.2.7 导航（RawControls）
- [x] 4.2.8 导航（Actions）
- [x] 4.2.9 焦点与消费
- [x] 4.2.10 坐标与 DPI
- [ ] 4.2.11 IME（可选，首版未接入 Windows IME）
- [x] 4.2.12 Demo 完整验收

---

## 4.3 步骤详解

### 4.3.1 确认版本与 API

先确定使用的 RmlUi 版本，并锁定以下 API 签名（3.x 与 4.x 有差异）：

| 功能 | RmlUi 3.x | RmlUi 4.x |
|---|---|---|
| 鼠标移动 | `ProcessMouseMove(int x, int y, int mods)` | 同 |
| 鼠标按键 | `ProcessMouseButtonDown/Up(int button, int mods)` | 同 |
| 滚轮 | `ProcessMouseWheel(int delta, int mods)` | `ProcessMouseWheel(float delta, int mods)` / `Vector2f` |
| 键盘 | `ProcessKeyDown/Up(KeyIdentifier, int mods)` | 同 |
| 文本 | `ProcessTextInput(Character)` | 还支持 `String` |
| 修饰键 | `KM_CTRL/SHIFT/ALT/META` | 增加 `KM_CAPSLOCK/NUMLOCK/SCROLLLOCK` |

用宏隔离：

```cpp
#if defined(RMLUI_VERSION_MAJOR) && RMLUI_VERSION_MAJOR >= 4
    #define INSYS_RMLUI4 1
#else
    #define INSYS_RMLUI4 0
#endif
```

---

### 4.3.2 RmlUiSystemInterface

文件：`include/insys/ui/rmlui_system_interface.h`、`src/ui/rmlui/rmlui_system_interface.cpp`。

```cpp
class RmlUiSystemInterface : public Rml::SystemInterface {
public:
    explicit RmlUiSystemInterface(ITime& time, IWindow& window);

    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
    void SetMouseCursor(const Rml::String& cursorName) override;
    void GetClipboardText(Rml::String& text) override;
    void SetClipboardText(const Rml::String& text) override;
    void GetSystemMetrics(Rml::SystemInterface::Metrics& metrics) override;

private:
    ITime& m_time;
    IWindow& m_window;
};
```

要点：

- `GetElapsedTime` 返回单调递增秒（与输入时间轴一致或独立，但不要回退）。
- `LogMessage` 输出到控制台/日志文件。
- `SetMouseCursor` 由宿主决定是否实现（GLFW 可设置标准光标）。
- 剪贴板：Windows 用 `OpenClipboard/GetClipboardData/SetClipboardData`。
- `GetSystemMetrics`：屏幕尺寸、DPI、滚动条尺寸等，按 RmlUi 版本要求填。

测试：

- `GetElapsedTime` 单调。
- 剪贴板往返（Windows）。

---

### 4.3.3 RmlUiInputAdapter 骨架

文件：

- `include/insys/ui/rmlui_input_adapter.h`
- `src/ui/rmlui/rmlui_input_adapter.cpp`

```cpp
namespace insys::ui {

enum class NavigationSource { RawControls, Actions };
enum class CapturePolicy { Full, PointerAndText, None };

class RmlUiInputAdapter {
public:
    RmlUiInputAdapter(System& system, Rml::Context* context);

    void set_navigation_source(NavigationSource s);
    void set_capture_policy(CapturePolicy p);

    void flush();   // 每帧在 system.update 之后调用

    bool pointer_over_ui() const;
    bool is_consuming_pointer() const;
    bool is_consuming_keyboard() const;
    bool is_consuming_text() const;

    // Action 驱动导航
    void set_ui_actions(ActionDef* navigate, ActionDef* submit,
                        ActionDef* cancel, ActionDef* point,
                        ActionDef* click, ActionDef* scroll);

private:
    void flush_pointer();
    void flush_keyboard();
    void flush_text();
    void flush_navigation_raw();
    void flush_navigation_actions();

    System& m_system;
    Rml::Context* m_context;
    NavigationSource m_navSource = NavigationSource::RawControls;
    CapturePolicy m_policy = CapturePolicy::PointerAndText;

    // 上一帧状态
    Vector2 m_lastMousePos;
    bool m_lastButtons[3] = {};
    Vector2 m_lastScroll;
    std::array<bool, 256> m_lastKeys{};
    bool m_lastNav[4] = {};

    bool m_pointerConsumed = false;
    bool m_keyboardConsumed = false;
    bool m_textConsumed = false;
};
}
```

---

### 4.3.4 鼠标映射

```cpp
void RmlUiInputAdapter::flush_pointer() {
    Mouse* mouse = m_system.mouse();
    if (!mouse) { reset_pointer_state(); return; }

    Vector2 pos = mouse->position()->read_value();
    Vector2 scroll = mouse->scroll()->read_value();

    // 坐标：窗口坐标 -> RmlUi 坐标（见 4.3.10）
    Vector2 uiPos = to_ui_space(pos);

    if (uiPos != m_lastMousePos) {
        m_pointerConsumed = m_context->ProcessMouseMove(
            int(uiPos.x), int(uiPos.y), current_mods());
        m_lastMousePos = uiPos;
    }

    process_button(0, mouse->left_button(), uiPos);
    process_button(1, mouse->right_button(), uiPos);
    process_button(2, mouse->middle_button(), uiPos);

#if INSYS_RMLUI4
    if (scroll.x != 0 || scroll.y != 0)
        m_context->ProcessMouseWheel({scroll.x, scroll.y}, current_mods());
#else
    if (scroll.y != 0)
        m_context->ProcessMouseWheel(int(scroll.y), current_mods());
#endif
}

void RmlUiInputAdapter::process_button(int index, Control* button, Vector2 pos) {
    bool pressed = button->read_value<float>() >= button_press_threshold();
    if (pressed == m_lastButtons[index]) return;

    // 某些 RmlUi 版本要求先移动再按键
    m_context->ProcessMouseMove(int(pos.x), int(pos.y), current_mods());
    bool consumed = pressed
        ? m_context->ProcessMouseButtonDown(index, current_mods())
        : m_context->ProcessMouseButtonUp(index, current_mods());
    m_pointerConsumed |= consumed;
    m_lastButtons[index] = pressed;
}
```

注意：

- RmlUi 的按钮索引：`0=左`、`1=右`、`2=中`（以版本为准）。
- 按钮按下前确保指针已移动到当前位置，否则可能点不中。
- 滚轮事件是“增量”，不要每帧重复发送；读取后需手动清零或记录上次值。
  - 当前后端把滚轮写入鼠标状态；适配器比较 `scroll != lastScroll` 去重，不依赖 `reset_scroll()`。

---

### 4.3.5 键盘映射

```cpp
Rml::Input::KeyIdentifier map_key(int keyCode);

void RmlUiInputAdapter::flush_keyboard() {
    Keyboard* kb = m_system.keyboard();
    if (!kb) return;

    for (int k = 0; k < KeyCount; ++k) {
        bool down = kb->key(k)->read_value<float>() > 0.5f;
        if (down == m_lastKeys[k]) continue;
        auto rmlKey = map_key(k);
        bool consumed = down
            ? m_context->ProcessKeyDown(rmlKey, current_mods())
            : m_context->ProcessKeyUp(rmlKey, current_mods());
        m_keyboardConsumed |= consumed;
        m_lastKeys[k] = down;
    }
}
```

映射表见总体设计 12.3。实现时：

- 用一个 `constexpr` 数组把内部 `KeyCode` 映射到 `Rml::Input::KeyIdentifier`。
- 未映射的键返回 `Rml::Input::KI_UNKNOWN` 并跳过。
- 修饰键状态每帧从 Keyboard 的 `shift/ctrl/alt/meta` 读取。

测试：

- A/Z/0/9/方向/Enter/Esc/Backspace 映射正确。

---

### 4.3.6 文本输入

两种来源：

1. GLFW `CharCallback` → 本库产生 `TextEvent`。
2. 本库把文本事件缓存到 `System::pending_text()`（或通过回调）。

适配器：

```cpp
void RmlUiInputAdapter::flush_text() {
    for (char32_t ch : m_system.take_pending_text()) {
        // 过滤控制字符（除了制表符）
        if (ch == U'\r') continue;
        m_textConsumed |= m_context->ProcessTextInput(Rml::Character(ch));
    }
}
```

注意：

- 文本输入与键盘按下是两套事件：文本框需要 `ProcessTextInput` 来插入字符，同时需要 `ProcessKeyDown` 来处理 Backspace/Enter/方向键。
- IME 组合文本（中文）需要额外处理（见 4.3.11）。
- 不要在 `CharCallback` 里直接调用 RmlUi（可能不在主线程/时机不对），统一在 `flush()` 中处理。

测试：

- 输入 ASCII 字符进入文本框。
- Backspace/Enter 生效。
- 中文输入（若实现 IME）。

---

### 4.3.7 导航（RawControls）

```cpp
void RmlUiInputAdapter::flush_navigation_raw() {
    Gamepad* pad = m_system.gamepad();
    if (!pad) { reset_nav(); return; }

    Vector2 dpad = pad->dpad()->read_value();
    Vector2 stick = pad->left_stick()->read_value();
    Vector2 nav = (dpad.length() > 0.1f) ? dpad : stick;

    bool up    = nav.y >  0.5f;
    bool down  = nav.y < -0.5f;
    bool left  = nav.x < -0.5f;
    bool right = nav.x >  0.5f;

    emit_nav_key(up,    Rml::Input::KI_UP);
    emit_nav_key(down,  Rml::Input::KI_DOWN);
    emit_nav_key(left,  Rml::Input::KI_LEFT);
    emit_nav_key(right, Rml::Input::KI_RIGHT);

    emit_nav_key(pad->button_south()->read_value<float>() > 0.5f, Rml::Input::KI_RETURN);
    emit_nav_key(pad->button_east()->read_value<float>() > 0.5f,  Rml::Input::KI_ESCAPE);
}
```

- 与键盘共用 `m_lastNav` 去重。
- deadzone 可配置。

---

### 4.3.8 导航（Actions）

```cpp
void RmlUiInputAdapter::flush_navigation_actions() {
    if (!m_navigate) return;

    Vector2 nav = m_navigate->read_value<Vector2>();
    bool up    = nav.y >  0.5f;
    bool down  = nav.y < -0.5f;
    bool left  = nav.x < -0.5f;
    bool right = nav.x >  0.5f;

    emit_nav_key(up, Rml::Input::KI_UP);
    emit_nav_key(down, Rml::Input::KI_DOWN);
    emit_nav_key(left, Rml::Input::KI_LEFT);
    emit_nav_key(right, Rml::Input::KI_RIGHT);

    emit_nav_key(m_submit && m_submit->was_pressed_this_frame(), Rml::Input::KI_RETURN);
    emit_nav_key(m_cancel && m_cancel->was_pressed_this_frame(), Rml::Input::KI_ESCAPE);
}
```

- Action 驱动模式便于改键、统一键鼠/手柄。
- `emit_nav_key` 只在状态变化时发送 Down/Up。

测试：

- 手柄方向键移动 RmlUi 焦点。
- A 确认、B 取消。

---

### 4.3.9 焦点与消费

```cpp
bool RmlUiInputAdapter::pointer_over_ui() const {
    return m_context->GetHoverElement() != m_context->GetRootElement();
}
```

策略：

```cpp
bool should_block_game_input() const {
    switch (m_policy) {
    case CapturePolicy::Full:
        return ui_has_focus();      // 例如有模态框/文本焦点
    case CapturePolicy::PointerAndText:
        return m_pointerConsumed || m_textConsumed || m_keyboardConsumed;
    case CapturePolicy::None:
        return false;
    }
}
```

- 游戏层每帧查询 `is_consuming_pointer/keyboard`，决定是否处理同一帧的 Action。
- 更稳妥的做法：在 Action 回调里检查 `InputBlockedByUI()`；或在调用游戏逻辑前统一判断。
- 若 UI 消费了指针，游戏不应再处理该点击；但移动/战斗等可能仍需要（按策略）。

测试：

- 点击 UI 按钮时，游戏 Fire action 不触发。
- 点击空白区域时，游戏 action 触发。
- 策略切换生效。

---

### 4.3.10 坐标与 DPI

RmlUi 使用像素坐标，`Context::SetDimensions` 必须与实际渲染尺寸一致。

- 若渲染到 framebuffer：使用 `glfwGetFramebufferSize`，坐标用 `mouse.position * contentScale`。
- 若渲染到窗口：使用 `glfwGetWindowSize`，坐标直接使用。
- Demo 中统一使用 framebuffer 坐标：

```cpp
Vector2 to_ui_space(Vector2 windowPos) {
    Vector2 scale = m_window.content_scale();
    return { windowPos.x * scale.x, windowPos.y * scale.y };
}
```

- 在 `Content Scale` 变化时（`glfwSetWindowContentScaleCallback`）更新 `Context::SetDimensions`。
- 验收时测试 125%/150%/200% 缩放。

---

### 4.3.11 IME（可选）

GLFW 不直接提供 IME 组合串。Windows 上可选方案：

1. 使用 Win32 `WM_IME_STARTCOMPOSITION/COMPOSITION/ENDCOMPOSITION` 消息，把组合串转发给 RmlUi（`ProcessTextInput`）。
2. 或调用 `ImmGetCompositionStringW`。
3. 需要窗口消息循环与 GLFW 集成（`glfwSetWindowUserPointer` + 子类化 `WndProc`）。

首版建议：

- 先支持 ASCII/直接输入。
- IME 作为 M4 的独立子项，不阻塞验收（若必须支持中文，则列为必做）。

测试：

- 中文输入法输入“你好”能进入文本框。

---

### 4.3.12 Demo 完整验收

Demo 页面：

1. **设备页**：设备列表、控件值。
2. **Action 页**：Move/Jump/Fire 的相位与值。
3. **UI 页**：按钮、文本输入、列表/滚动、手柄导航。
4. **重绑定页**：M3 功能。

验收清单（与总体设计 14.3 一致）：

- [x] 键盘：A–Z、0–9、F1–F12、Shift 组合、Backspace/Enter/Tab/方向键（键映射与文本框测试）。
- [x] 鼠标：悬停、左/右/中键、滚轮、进入/离开；高 DPI 逻辑已接入，实际缩放手工验收待做。
- [ ] 手柄：按钮、摇杆、扳机、D-Pad、热插拔。
- [x] 手柄导航：方向移动焦点、A 确认、B 取消（RawControls 与 Actions 单元测试）。
- [x] 文本输入：ASCII 正常；IME 未实现（可选项）。
- [x] UI 门控：UI 上点击不触发游戏 action；空白区域触发（Demo 与策略测试）。
- [x] 调试面板：设备/控件/事件/Action 与 UI 消费状态实时刷新。

---

## 4.4 测试清单

- [x] `test_rmlui_key_map.cpp`：键映射。
- [x] `test_rmlui_adapter.cpp`：用 Fake UiEventSink 验证调用序列、策略、导航与 Action 指针。
- [x] `test_rmlui_event_sink.cpp`：真实 RmlUi Context 上的鼠标/点击/文本/退格。
- [x] `test_rmlui_system_interface.cpp`：时间、光标回调与日志 hook。
- [ ] 真机手柄热插拔与 125%/150%/200% DPI 手工验收。

> 说明：RmlUi 的 `Context` 不易 mock，可抽出一个 `IUiEventSink` 接口，把 `ProcessMouseMove/KeyDown/TextInput` 包装成虚函数，测试时用假实现记录调用。

---

## 4.4.1 实现说明

- 实际使用 vcpkg 的 **RmlUi 6.3**。6.3 的 `SystemInterface` 没有 `GetSystemMetrics` 虚函数，因此不覆盖该 API；时钟、日志、剪贴板、光标与键盘激活 hook 已实现。
- RmlUi 的 `ProcessKeyDown/ProcessTextInput` 返回“未被消费”，`ProcessMouseMove/Button/Wheel` 返回“未与元素交互”。`RmlUiEventSink` 统一反转为 `UiEventSink` 的“true = UI 消费”，避免门控方向错误。
- `UiInputAdapter` 不依赖 RmlUi，测试使用 `FakeSink` 检查调用序列；`RmlUiInputAdapter` 只是其 RmlUi 包装。
- 鼠标坐标按 framebuffer/window 比例换算；Demo 使用 framebuffer 作为 `Context` 尺寸并设置 `SetDensityIndependentPixelRatio`。
- 滚轮沿用 RmlUi 官方 GLFW 后端约定：GLFW 向上滚动的正值转换为 RmlUi 的负值。
- 手柄导航支持 RawControls 和 Actions 两种模式；Demo 的 Actions 模式使用 `assets/ui.json`。
- Demo 的 Action 门控读取上一帧 flush 的 UI 消费状态，因此 UI 点击不会触发同一帧的游戏 Fire；首次创建窗口前的输入不参与门控。
- **IME 未实现**（4.2.11 可选）：当前仅接入 GLFW 已提交的 UTF-32 文本字符，Windows IME 组合串需要额外的 `WM_IME_*` 接入。
- Demo 退出测试暴露并修复了 `Manager::remove_device` 的旧 use-after-free：先移除所有 monitor，再销毁设备。

## 4.5 验收标准

- [ ] Demo 能完整完成第 4.3.12 节清单。
- [ ] 不启用 RmlUi 自带 GLFW backend 输入；无重复点击。
- [ ] 高 DPI 坐标正确。
- [ ] UI 消费时游戏输入被正确门控。
- [ ] 无崩溃/泄漏。

---

## 4.6 常见问题

1. **重复点击**：同时启用了 RmlUi `RmlUi_Backend_GLFW` 的输入处理。只使用 Renderer。
2. **点击偏位**：`Context::SetDimensions` 与坐标空间不一致（窗口 vs framebuffer）。
3. **滚轮连续触发**：`scroll` 是增量，未清零/未去重。
4. **文本框无法退格**：只发了 `ProcessTextInput`，没发 `ProcessKeyDown/Up`。
5. **手柄导航抖动**：摇杆未加 deadzone 或未去重。

---

## 4.7 下一步

进入 [M5-Debugging-Stability.md](M5-Debugging-Stability.md)。
