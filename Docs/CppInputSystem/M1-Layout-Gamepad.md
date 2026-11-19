# M1 —— 布局系统 + ControlPath + DeviceBuilder + 手柄

> 前置：M0 完成。
> 目标：用通用布局系统替换 M0 的手工设备构造；实现 ControlPath；实现 Keyboard/Mouse/Gamepad/Joystick 完整布局；GLFW 手柄轮询与热插拔。
> 参考：`../CppInputSystem_ImplementationPlan.md` 第 8 章、10.3 节。

---

## 1.1 目标与交付物

- `Layout` / `ControlItem` / `LayoutRegistry`。
- `DeviceBuilder`（含 `compute_state_layout`、`ApplyUseStateFrom`、`FinalizeControlHierarchy`）。
- `ControlPath` 解析、匹配、查找。
- Keyboard / Mouse / Gamepad / Joystick 的完整布局与状态结构。
- `DeviceDescription` / `DeviceMatcher`。
- `GlfwBackend` 手柄枚举、轮询、diff、热插拔。
- Demo 增加手柄面板与设备列表。

---

## 1.2 任务清单

- [x] 1.2.1 Layout 数据模型
- [x] 1.2.2 LayoutRegistry
- [x] 1.2.3 ControlItem 合并与继承
- [x] 1.2.4 ControlPath
- [x] 1.2.5 DeviceBuilder
- [x] 1.2.6 compute_state_layout
- [x] 1.2.7 FinalizeControlHierarchy
- [x] 1.2.8 Keyboard 布局
- [x] 1.2.9 Mouse 布局
- [x] 1.2.10 Gamepad 布局
- [x] 1.2.11 Joystick 布局
- [x] 1.2.12 DeviceDescription / Matcher
- [x] 1.2.13 GLFW 手柄后端
- [x] 1.2.14 Demo 手柄面板
- [x] 1.2.15 测试

---

## 1.3 步骤详解

### 1.3.1 Layout 数据模型

文件：`include/insys/control_layout.h`、`src/core/control_layout.cpp`。

```cpp
struct NamedValue {
    std::string name;
    PrimitiveValue value;
};

struct NameAndParameters {
    std::string name;
    std::vector<NamedValue> parameters;
};

struct ControlItem {
    InternedString name;
    InternedString layout;
    InternedString variants;
    std::string useStateFrom;
    std::string displayName;
    std::string shortDisplayName;

    SmallVector<InternedString, 2> usages;
    SmallVector<InternedString, 2> aliases;
    std::vector<NamedValue> parameters;
    std::vector<NameAndParameters> processors;

    uint32_t offset = StateBlock::AutomaticOffset;
    uint32_t bit = 0;
    uint32_t sizeInBits = 0;
    FourCC format{};
    int arraySize = 0;

    std::optional<PrimitiveValue> defaultState;
    std::optional<PrimitiveValue> minValue;
    std::optional<PrimitiveValue> maxValue;

    bool isModifyingExistingControl = false;
    bool isNoisy = false;
    bool isSynthetic = false;
    bool dontReset = false;
    bool isFirstDefinedInThisLayout = false;

    // 合并：未设置的字段从 other 继承
    ControlItem merge(const ControlItem& other) const;
};

class Layout {
public:
    InternedString name;
    InternedString variants;
    FourCC stateFormat{};
    std::string displayName;
    std::optional<bool> updateBeforeRender;
    std::optional<bool> canRunInBackground;

    std::vector<ControlItem> controls;
    Layout* baseLayout = nullptr;
    std::function<std::unique_ptr<Control>()> factory;

    // 从 base 合并 controls
    void merge_from(const Layout& base);
};
```

`merge_from` 规则：

- 先继承 base 的所有 `ControlItem`。
- 子布局同名 `ControlItem` 覆盖/合并。
- 标记 `isFirstDefinedInThisLayout`。

测试：

- 子布局继承父布局控件。
- 同名控件字段覆盖。
- 路径形式控件（`leftStick/x`）标记为 `isModifyingExistingControl`。

---

### 1.3.2 LayoutRegistry

文件：`include/insys/layout_registry.h`、`src/core/layout_registry.cpp`。

```cpp
class LayoutRegistry {
public:
    void add(std::unique_ptr<Layout> layout);
    Layout* find(std::string_view name);
    const Layout* find(std::string_view name) const;

    void add_matcher(std::string_view layoutName, const DeviceMatcher& matcher);
    std::string find_matching_layout(const DeviceDescription& desc) const;

    std::vector<std::string> list() const;
};
```

- 名称大小写不敏感（内部统一小写）。
- 支持 `name:namespace` 形式，查找时忽略命名空间。
- matcher 按匹配分数排序，取最高分。

测试：

- 注册/查找。
- matcher 命中与分数。
- 未知布局返回 null。

---

### 1.3.3 ControlPath

文件：`include/insys/control_path.h`、`src/core/control_path.cpp`。

语法（总体设计 8.2）：

```
path        := component ('/' component)*
component   := usage* layout | name
usage       := '{' identifier '}'
layout      := '<' identifier '>'
wildcard    := '*'
```

```cpp
struct PathComponent {
    InternedString name;
    InternedString layout;
    SmallVector<InternedString, 2> usages;
    bool wildcard = false;
    bool isRoot = false;
};

std::vector<PathComponent> parse_path(std::string_view path);
bool path_matches(std::string_view path, const Control& control);
Control* find_control(Control& root, std::string_view path);
std::string combine_path(const Control* parent, std::string_view name);
std::string to_display_string(std::string_view path);
```

匹配规则：

- 大小写不敏感。
- 首段可匹配设备 layout、设备 usage 或 `*`。
- 后续段匹配控件名、usage、layout。
- 支持 alias。
- 非法路径返回空分量列表并记录诊断。

测试：

- `Keyboard/Space` 命中。
- `Gamepad/LeftStick/X` 命中子控件。
- `*/{PrimaryAction}` 命中多个设备上的 usage。
- `*` 通配。
- 非法路径。

---

### 1.3.4 DeviceBuilder

文件：`include/insys/device_builder.h`、`src/core/device_builder.cpp`。

```cpp
class DeviceBuilder {
public:
    std::unique_ptr<Device> build(std::string_view layoutName,
                                  std::string_view variants,
                                  int deviceId,
                                  const DeviceDescription& description);
private:
    Control* instantiate(Layout& layout, InternedString variants,
                         InternedString name, Control* parent);
    void add_child_controls(Layout& layout, InternedString variants, Control* parent);
    Control* add_child_control(Layout& layout, InternedString variants, Control* parent,
                               ControlItem& item, int childIndex);
    void compute_state_layout(Control* control);
    void finalize_control_hierarchy(Device* device);
    void apply_use_state_from(Control* parent, const ControlItem& item, const Layout& layout);

    Device* m_device = nullptr;
    LayoutRegistry* m_registry = nullptr;
    std::unordered_map<std::string, ControlItem> m_childOverrides;
};
```

`build` 流程见总体设计 8.5。

关键实现细节：

1. `instantiate` 必须是根设备时初始化 `m_device`，设置 `stateBlock.byteOffset = 0`、`format = layout.stateFormat`。
2. 控件名默认取 layout 名，去掉命名空间前缀，清理 `/`。
3. `variants` 默认 `layout.variants`，再默认 `"Default"`。
4. `add_child_control` 设置 flags、默认值、min/max、offset/bit/size/format、usages、aliases、parameters、processors。
5. `useStateFrom` 控件：`stateBlock.sizeInBits = kUseStateFromOtherControl`，稍后 `apply_use_state_from`。
6. 子控件路径覆盖（`leftStick/x`）：存入 `m_childOverrides`，在 `add_child_control` 中合并。

测试：

- 简单布局构建。
- 继承布局构建。
- 路径覆盖（`leftStick/x`）。
- useStateFrom。
- 数组控件（可选）。

---

### 1.3.5 compute_state_layout

这是 M1 最关键的算法，必须严格按总体设计 8.5 实现。

```cpp
void DeviceBuilder::compute_state_layout(Control* control) {
    // 1. format 可推出尺寸
    if (control->stateBlock.sizeInBits == 0 && control->stateBlock.format != FourCC{}) {
        auto bits = primitive_format_bits(control->stateBlock.format);
        if (bits > 0) control->stateBlock.sizeInBits = bits;
    }
    // 2. 无尺寸且无子控件 → 错误
    if (control->stateBlock.sizeInBits == 0 && control->childCount == 0) {
        throw std::runtime_error("control has no size and no children");
    }
    // 3. 有子控件 → 计算
    if (control->childCount == 0) return;

    uint32_t firstUnfixedByteOffset = 0;

    // 3.1 固定偏移子控件
    for (Control* child : children_of(control)) {
        if (child->stateBlock.sizeInBits == kUseStateFromOtherControl) continue;
        if (child->stateBlock.byteOffset == StateBlock::InvalidOffset ||
            child->stateBlock.byteOffset == StateBlock::AutomaticOffset) continue;
        if (child->stateBlock.bitOffset == StateBlock::InvalidOffset)
            child->stateBlock.bitOffset = 0;
        uint32_t end = following_byte_offset(child->stateBlock.byteOffset,
                                             child->stateBlock.bitOffset +
                                             child->stateBlock.sizeInBits);
        firstUnfixedByteOffset = std::max(firstUnfixedByteOffset, end);
    }

    // 3.2 自动偏移子控件
    uint32_t runningByteOffset = firstUnfixedByteOffset;
    Control* firstBitAddressingChild = nullptr;
    uint32_t bitfieldSizeInBits = 0;

    for (Control* child : children_of(control)) {
        auto& sb = child->stateBlock;
        if (sb.byteOffset != StateBlock::InvalidOffset &&
            sb.byteOffset != StateBlock::AutomaticOffset) continue;
        if (sb.sizeInBits == kUseStateFromOtherControl) continue;

        bool isBitAddressing = (sb.sizeInBits % 8) != 0;
        if (isBitAddressing) {
            if (!firstBitAddressingChild) firstBitAddressingChild = child;
            if (sb.bitOffset == StateBlock::InvalidOffset ||
                sb.bitOffset == StateBlock::AutomaticOffset) {
                sb.bitOffset = bitfieldSizeInBits;
                bitfieldSizeInBits += sb.sizeInBits;
            } else {
                bitfieldSizeInBits = std::max(bitfieldSizeInBits,
                                              sb.bitOffset + sb.sizeInBits);
            }
        } else {
            if (firstBitAddressingChild) {
                runningByteOffset = following_byte_offset(runningByteOffset, bitfieldSizeInBits);
                firstBitAddressingChild = nullptr;
            }
            if (sb.bitOffset == StateBlock::InvalidOffset) sb.bitOffset = 0;
            runningByteOffset = align_natural(runningByteOffset, sb.aligned_size_in_bytes());
        }

        sb.byteOffset = runningByteOffset;

        if (!isBitAddressing) {
            runningByteOffset = following_byte_offset(runningByteOffset, sb.sizeInBits);
        }
    }

    if (firstBitAddressingChild) {
        runningByteOffset = following_byte_offset(runningByteOffset, bitfieldSizeInBits);
    }

    control->stateBlock.sizeInBits = runningByteOffset * 8;
}
```

工具：

```cpp
uint32_t following_byte_offset(uint32_t byteOffset, uint32_t bitOffsetPlusSize) {
    return byteOffset + ((bitOffsetPlusSize + 7) >> 3);
}
uint32_t align_natural(uint32_t offset, uint32_t size) {
    uint32_t a = size >= 8 ? 8 : (size >= 4 ? 4 : (size >= 2 ? 2 : 1));
    return (offset + a - 1) & ~(a - 1);
}
```

测试：

- 全位域（Keyboard）：每个键 1 bit，父 size = ceil(bits/8)。
- 混合固定 + 自动偏移。
- 位域后接 float/Vector2，验证对齐。
- 空控件报错。

---

### 1.3.6 FinalizeControlHierarchy

参考 `InputDeviceBuilder.FinalizeControlHierarchy`。

职责：

1. 深度优先遍历，填充 `device.allControls`（**父子连续**）。
2. 填充每个控件的 `childStart/childCount`。
3. 填充 `device.usagesForEachControl` / `usageToControl`。
4. 填充 `device.aliasesForEachControl`。
5. 生成 `stateOffsetToControlMap`（可选，用于调试/快速查找）。
6. 生成控件树节点（可选，用于按偏移快速定位控件）。

```cpp
void DeviceBuilder::finalize_control_hierarchy(Device* device) {
    device->allControls.clear();
    // 递归添加，先父后子
    std::function<void(Control*)> visit = [&](Control* c) {
        c->childStart = uint32_t(device->allControls.size());
        device->allControls.push_back(c);
        // 子控件在下面按顺序加入
        for (Control* child : children_of(c)) visit(child);
        c->childCount = uint32_t(device->allControls.size()) - c->childStart - 1;
    };
    visit(device);
}
```

> “父子连续”指同一父控件的所有子控件在数组中连续，便于 `children()` 用 slice 返回。

测试：

- 控件数量与层级正确。
- `children()` 返回正确切片。
- usage/alias 索引正确。

---

### 1.3.7 Keyboard 布局

参考 `Keyboard.cs`。

```cpp
struct KeyboardState {
    static constexpr FourCC format = FourCC('K','E','Y','S');
    uint8_t keys[(KeyCount + 7) / 8];
};
```

布局构建：

```cpp
Layout make_keyboard_layout() {
    Layout l;
    l.name = "Keyboard";
    l.stateFormat = KeyboardState::format;
    l.factory = [] { return std::make_unique<Keyboard>(); };

    // anyKey（合成）
    l.controls.push_back(ControlItem{
        .name = "anyKey", .layout = "AnyKey",
        .bit = 1, .sizeInBits = Keyboard::KeyCount, .isSynthetic = true
    });

    // 每个键
    for (int i = 0; i < KeyCount; ++i) {
        ControlItem item;
        item.name = key_name(i);
        item.layout = "Key";
        item.bit = i;
        item.usages = key_usages(i);     // Back/Submit/Modifier...
        item.displayName = key_display_name(i);
        l.controls.push_back(item);
    }

    // shift/ctrl/alt/meta 合成（2 bits）
    add_discrete_button(l, "shift", Key::LeftShift);
    add_discrete_button(l, "ctrl",  Key::LeftCtrl);
    add_discrete_button(l, "alt",   Key::LeftAlt);
    add_discrete_button(l, "meta",  Key::LeftMeta);
    return l;
}
```

`add_discrete_button`：

```cpp
ControlItem item;
item.name = name;
item.layout = "DiscreteButton";
item.bit = key;
item.sizeInBits = 2;
item.isSynthetic = true;
item.parameters = { {"minValue", 1}, {"maxValue", 3}, {"writeMode", 1} };
item.usages = { "Modifier" };
```

`Keyboard` 类：

```cpp
class Keyboard : public Device {
public:
    Control* anyKey();
    KeyControl* this_key(KeyCode k);
    KeyControl* find_key(std::string_view name);
    static constexpr int KeyCount = ...; // 至少覆盖 A-Z/0-9/功能键
};
```

测试：

- 构建后每个键 `bitOffset` 正确。
- 按下 A 后 `a.isPressed()` 为 true。
- Shift 合成在左右 Shift 任一按下时为 1。

---

### 1.3.8 Mouse 布局

参考 `Mouse.cs`。

```cpp
struct MouseState {
    static constexpr FourCC format = FourCC('M','O','U','S');
    Vector2 position;
    Vector2 delta;
    Vector2 scroll;
    uint16_t buttons;
    uint16_t displayIndex;
    uint16_t clickCount;
};
static_assert(sizeof(MouseState) == 30);
```

布局：

- `position`（usage `Point`，dontReset）
- `delta`（usage `Secondary2DMotion`）
- `scroll`（含子控件 `scroll/x`、`scroll/y`）
- `leftButton/rightButton/middleButton/forwardButton/backButton`（Button，bit 0..4）
- `pressure`（Axis，自动偏移）
- `radius`（Vector2，自动偏移）
- `pointerId`（Digital，自动偏移）
- `displayIndex`、`clickCount`

测试：

- `position` 读写正确。
- 按钮位映射正确。
- scroll 子控件偏移正确。

---

### 1.3.9 Gamepad 布局

参考 `Gamepad.cs`。

```cpp
struct GamepadState {
    static constexpr FourCC format = FourCC('G','P','A','D');
    uint32_t buttons;
    Vector2 leftStick;
    Vector2 rightStick;
    float leftTrigger;
    float rightTrigger;
};
static_assert(sizeof(GamepadState) == 28);
```

布局：

- `buttons` 位域：dpad 4 bits + 10 个按钮。
- `leftStick/rightStick`：`StickControl`，`format=VEC2`，`processors="stickDeadzone"`。
- `leftTrigger/rightTrigger`：`ButtonControl`，`format=FLT`。
- `dpad`：`DpadControl`，`format=BIT`，4 bits。
- usages 与别名见总体设计 8.6。

测试：

- 按钮位映射。
- 摇杆值。
- 扳机值。
- dpad 合成。

---

### 1.3.10 Joystick 布局

参考 `Joystick.cs`。

```cpp
struct JoystickState {
    static constexpr FourCC format = FourCC('J','O','Y','S');
    uint32_t buttons;
    Vector2 stick;
    float trigger;
};
```

布局：

- `stick`：`StickControl`
- `trigger`：`AxisControl`
- `buttons[]`：最多 20 个按钮

测试：

- 基本读写。

---

### 1.3.11 DeviceDescription / Matcher

文件：`include/insys/device_description.h`、`src/core/device_matcher.cpp`。

```cpp
struct DeviceDescription {
    std::string interfaceName;
    std::string deviceClass;
    std::string manufacturer;
    std::string product;
    std::string serial;
    std::string version;
    std::string capabilities;
};

class DeviceMatcher {
public:
    void add(std::string_view key, std::string_view pattern, bool regex = false);
    float match_score(const DeviceDescription& desc) const;
};
```

- 缺字段不参与匹配（视为通过）。
- 没有 matcher 时返回 0。
- 全部匹配返回 1。

测试：

- 精确/前缀/正则匹配。
- 分数排序。

---

### 1.3.12 GLFW 手柄后端

文件：`src/backend/glfw/glfw_gamepad_map.cpp`、`glfw_backend.cpp`。

实现要点：

```cpp
void GlfwBackend::poll_gamepads() {
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickIsGamepad(jid)) continue;
        GLFWgamepadstate s;
        if (!glfwGetGamepadState(jid, &s)) continue;

        GamepadState state = map_gamepad(s);
        auto it = m_gamepads.find(jid);
        if (it == m_gamepads.end()) {
            // 新设备：分配 deviceId，创建设备，发 discovered
            ...
        } else if (memcmp(&it->second, &state, sizeof(state)) != 0) {
            // 状态变化：发 DeltaStateEvent
            emit_state_event(device_id_for(jid), &state, sizeof(state));
            it->second = state;
        }
    }
}
```

`map_gamepad`：

```cpp
GamepadState map_gamepad(const GLFWgamepadstate& g) {
    GamepadState s{};
    s.buttons = 0;
    if (g.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP])    s.buttons |= 1u << 0;
    if (g.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN])  s.buttons |= 1u << 1;
    if (g.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT])  s.buttons |= 1u << 2;
    if (g.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT]) s.buttons |= 1u << 3;
    if (g.buttons[GLFW_GAMEPAD_BUTTON_Y])          s.buttons |= 1u << 4; // North
    if (g.buttons[GLFW_GAMEPAD_BUTTON_B])          s.buttons |= 1u << 5; // East
    if (g.buttons[GLFW_GAMEPAD_BUTTON_A])          s.buttons |= 1u << 6; // South
    if (g.buttons[GLFW_GAMEPAD_BUTTON_X])          s.buttons |= 1u << 7; // West
    if (g.buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB]) s.buttons |= 1u << 8;
    if (g.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB])s.buttons |= 1u << 9;
    if (g.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER])s.buttons |= 1u << 10;
    if (g.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER])s.buttons |= 1u << 11;
    if (g.buttons[GLFW_GAMEPAD_BUTTON_START])      s.buttons |= 1u << 12;
    if (g.buttons[GLFW_GAMEPAD_BUTTON_BACK])       s.buttons |= 1u << 13;

    s.leftStick  = { g.axes[GLFW_GAMEPAD_AXIS_LEFT_X],  g.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] };
    s.rightStick = { g.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], g.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] };
    s.leftTrigger  = (g.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f;
    s.rightTrigger = (g.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;
    return s;
}
```

热插拔：

```cpp
glfwSetJoystickCallback([](int jid, int event) {
    if (event == GLFW_CONNECTED) {
        // 下一帧 poll_gamepads 会发现并创建设备
    } else if (event == GLFW_DISCONNECTED) {
        // 产生 DeviceRemoveEvent
    }
});
```

注意：

- GLFW 回调是全局的，需要把 `this` 存到某个全局/静态映射（GLFW 不提供 joystick user pointer，可用 `glfwSetWindowUserPointer` + 全局后端指针）。
- 创建/删除设备会触发状态缓冲重分配，不要在回调中直接做；标记为 pending，在 `poll()` 中处理。

测试：

- 模拟 gamepad state 映射。
- 热插拔事件队列。

---

### 1.3.13 Demo 手柄面板

在 Demo 中增加：

- 设备列表：名称、layout、deviceId、连接状态。
- 手柄可视化：按钮亮灭、摇杆位置、扳机进度。
- 键盘按键高亮（可选）。

验收：

- 插拔手柄，列表实时更新。
- 按钮/摇杆值实时显示。

---

### 1.3.14 测试清单

- [x] `test_layout_builder.cpp`：继承、覆盖、偏移、位域、对齐、useStateFrom。
- [x] `test_control_path.cpp`：解析、匹配、通配、usage。
- [x] `test_device_keyboard.cpp`：键位、Shift 合成。
- [x] `test_device_mouse.cpp`：位置/按钮/滚轮。
- [x] `test_device_gamepad.cpp`：按钮/摇杆/扳机/dpad。
- [x] `test_device_matcher.cpp`：匹配分数。
- [ ] `test_glfw_gamepad_map.cpp`：映射正确（映射实现已完成，测试待补）。

---

## 1.4 验收标准

- [x] 所有设备通过 `DeviceBuilder` 构建，M0 的手工构造函数已删除。
- [x] 每个控件的 `byteOffset/bitOffset/sizeInBits` 与总体设计一致。
- [x] ControlPath 能定位任意设备/控件。
- [x] 手柄按钮/摇杆/扳机值正确（通过合成 GamepadState 单元测试验证；未接真实手柄）。
- [x] 热插拔不崩溃，设备列表更新（实现完成；真实手柄插拔需人工复验）。
- [x] `ctest` 全绿。

---

## 1.5 常见问题

1. **位域父大小错误**：忘记在 `compute_state_layout` 末尾提交最后的位域组。
2. **对齐错误**：非位域控件未按自然对齐推进，导致 ARM 崩溃。
3. **控件数组不连续**：`finalize_control_hierarchy` 必须深度优先且父子连续。
4. **useStateFrom 控件参与布局**：必须跳过并单独 `apply_use_state_from`。
5. **GLFW joystick 回调重入**：不要在回调里创建设备，标记 pending。

---

## 1.6 下一步

进入 [M2-Actions-Basics.md](M2-Actions-Basics.md)。
