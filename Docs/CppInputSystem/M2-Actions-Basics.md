# M2 —— Actions 基础

> 前置：M1 完成。实现状态：M2 已完成（交互对象 Tap/Hold 等在 M3 实现；M2 的绑定解析接口已预留）。
> 目标：实现 Action / ActionMap / ActionAsset、BindingResolver、ActionState（SoA + 监视器）、默认交互状态机、Processor、Composite、启用/禁用、初始状态检查、冲突消解。
> 参考：`../CppInputSystem_ImplementationPlan.md` 第 11 章。

---

## 2.1 目标与交付物

- `ActionType` / `ActionPhase` / `Binding` / `ActionDef` / `ActionMap` / `ActionAsset`。
- `BindingResolver`：把绑定解析为控件、实例化处理器/交互/合成器、填充 SoA。
- `ActionState`：实现 `IStateChangeMonitor`，驱动相位状态机。
- `IProcessor` + 首版处理器。
- `IComposite` + Axis/Vector2/Vector3 合成。
- 默认交互（无 Interaction 时）。
- 启用/禁用、初始状态检查、冲突消解。
- Demo：输入可视化（显示 action 相位/值）。

---

## 2.2 任务清单

- [x] 2.2.1 Action 数据模型
- [x] 2.2.2 Binding / Processor / Interaction 描述解析
- [x] 2.2.3 IProcessor 与首版处理器
- [x] 2.2.4 IComposite 与首版合成器
- [x] 2.2.5 ActionState 内存布局
- [x] 2.2.6 BindingResolver
- [x] 2.2.7 ActionState 启用/禁用
- [x] 2.2.8 状态变化回调与 TriggerState
- [x] 2.2.9 默认交互状态机
- [x] 2.2.10 冲突消解
- [x] 2.2.11 初始状态检查
- [x] 2.2.12 回调（started/performed/canceled）
- [x] 2.2.13 Demo 输入可视化
- [x] 2.2.14 测试

---

## 2.3 步骤详解

### 2.3.1 Action 数据模型

文件：

- `include/insys/actions/action.h`
- `include/insys/actions/action_map.h`
- `include/insys/actions/action_asset.h`
- `include/insys/actions/binding.h`

```cpp
enum class ActionType { Value, Button, PassThrough };

enum class ActionPhase {
    Disabled, Waiting, Started, Performed, Canceled
};

struct Binding {
    std::string name;
    Guid id;
    std::string path;
    std::string overridePath;
    std::string action;
    std::string interactions;
    std::string processors;
    std::string groups;
    std::string overrideInteractions;
    std::string overrideProcessors;
    bool isComposite = false;
    bool isPartOfComposite = false;
    int partIndex = -1;

    std::string_view effective_path() const {
        return overridePath.empty() ? path : overridePath;
    }
    std::string_view effective_interactions() const {
        return overrideInteractions.empty() ? interactions : overrideInteractions;
    }
    std::string_view effective_processors() const {
        return overrideProcessors.empty() ? processors : overrideProcessors;
    }
};

struct ActionDef {
    std::string name;
    Guid id;
    ActionType type = ActionType::Button;
    std::string expectedControlType;
    std::string processors;
    std::string interactions;
    bool initialStateCheck = false;
    int priority = 0;

    // 运行时：由 ActionState 填充/使用，不对应资产
    ActionState* state = nullptr;
    int actionIndex = -1;

    bool is_button() const { return type == ActionType::Button; }
    bool is_value() const { return type == ActionType::Value; }
    bool is_pass_through() const { return type == ActionType::PassThrough; }
};
```

`ActionMap`：

```cpp
class ActionMap {
public:
    std::string name;
    Guid id;
    std::vector<ActionDef> actions;
    std::vector<Binding> bindings;

    void enable();
    void disable();
    bool enabled() const { return m_enabledActions > 0; }

    ActionDef* find_action(std::string_view nameOrId, bool throwIfNotFound = false);
    ActionDef* operator[](std::string_view name) { return find_action(name, true); }

private:
    int m_enabledActions = 0;
};
```

`ActionAsset`：

```cpp
class ActionAsset {
public:
    std::string name;
    std::vector<ActionMap> maps;
    std::vector<ControlScheme> controlSchemes;
    std::optional<std::vector<Device*>> devices;
    std::optional<Binding> bindingMask;

    void enable();
    void disable();
    ActionMap* find_map(std::string_view nameOrId, bool throwIfNotFound = false);
    ActionDef* find_action(std::string_view nameOrId, bool throwIfNotFound = false);
};
```

注意：

- `ActionDef` 是“定义”，`ActionState` 是“执行状态”。二者分离，避免把运行时字段塞进资产结构。
- 也可以像 Unity 一样让 `Action` 同时持有定义与指向 state 的指针；本实现选择显式分离，便于资产复用/多套状态。

测试：

- 查找 action/map。
- 启用/禁用计数。

---

### 2.3.2 描述字符串解析

需要解析：

- `interactions`：`"tap;hold(duration=0.3)"`。
- `processors`：`"stickDeadzone(min=0.2,max=0.9);scale(factor=2)"`。
- composite 路径：`"2DVector(mode=0)"`。

```cpp
struct ParsedParameter { std::string name; PrimitiveValue value; };
struct ParsedItem {
    std::string name;
    std::vector<ParsedParameter> parameters;
};

ParsedItem parse_item(std::string_view text);
std::vector<ParsedItem> parse_list(std::string_view text);  // ';' 分隔
```

规则：

- 名称大小写不敏感。
- 参数 `key=value`，逗号分隔。
- 值支持 bool/int/float/string/枚举（按目标字段类型转换）。
- 空字符串返回空列表。

测试：

- 无参数、单参数、多参数。
- 多个 item。
- 非法输入报诊断。

---

### 2.3.3 IProcessor 与首版处理器

文件：

- `include/insys/actions/processor.h`
- `src/actions/processors.cpp`

```cpp
enum class ValueType { None, Float, Vector2, Vector3, Quaternion, Int, Double };

class IProcessor {
public:
    virtual ~IProcessor() = default;
    virtual ValueType value_type() const = 0;
    virtual void process(void* value, size_t size, const Control* control) const = 0;
    enum class CachingPolicy { CacheResult, EvaluateOnEveryRead };
    virtual CachingPolicy caching_policy() const { return CachingPolicy::CacheResult; }
};
```

首版实现：

| Processor | 参数 | 实现 |
|---|---|---|
| `Invert` | | `v = -v` |
| `InvertVector2` | `invertX`, `invertY` | 分轴取反 |
| `InvertVector3` | `invertX/Y/Z` | |
| `Scale` | `factor` | `v *= factor` |
| `ScaleVector2` | `x`, `y` | |
| `ScaleVector3` | `x`, `y`, `z` | |
| `Normalize` | `min`, `max`, `zero` | `(v-min)/(max-min)`，可选 zero |
| `NormalizeVector2/3` | | 向量长度归一化 |
| `Clamp` | `min`, `max` | `clamp(v,min,max)` |
| `AxisDeadzone` | `min`, `max` | 单轴死区 |
| `StickDeadzone` | `min`, `max` | 径向死区 |

StickDeadzone 算法（参考 Unity）：

```
magnitude = length(v)
if magnitude < min: return (0,0)
if magnitude > max: return normalize(v)
normalized = (magnitude - min) / (max - min)
return normalize(v) * normalized
```

测试：

- 每个处理器数值正确。
- 参数默认值。

---

### 2.3.4 IComposite 与首版合成器

文件：

- `include/insys/actions/composite.h`
- `src/actions/composites.cpp`

```cpp
class IComposite {
public:
    virtual ~IComposite() = default;
    virtual ValueType value_type() const = 0;
    virtual void read_value(CompositeContext& ctx, void* out) const = 0;
    virtual float evaluate_magnitude(CompositeContext& ctx) const = 0;
    virtual int assign_part(std::string_view name, int& currentPartIndex) = 0;
};
```

`CompositeContext`：

```cpp
class CompositeContext {
public:
    template <typename T> T read_value(int partIndex) const;
    bool read_value_as_button(int partIndex) const;
    float magnitude() const;
};
```

首版：

- `Axis`：`positive - negative`。
- `Vector2`：up/down/left/right；mode = DigitalNormalized / Digital / Analog。
- `Vector3`：up/down/left/right/forward/backward。

Vector2 实现：

```cpp
void Vector2Composite::read_value(CompositeContext& ctx, void* out) const {
    auto* v = static_cast<Vector2*>(out);
    if (mode == Mode::Analog) {
        float upV = ctx.read_value<float>(up);
        float downV = ctx.read_value<float>(down);
        float leftV = ctx.read_value<float>(left);
        float rightV = ctx.read_value<float>(right);
        *v = make_dpad_vector(upV, downV, leftV, rightV);
        return;
    }
    bool u = ctx.read_value_as_button(up);
    bool d = ctx.read_value_as_button(down);
    bool l = ctx.read_value_as_button(left);
    bool r = ctx.read_value_as_button(right);
    *v = make_dpad_vector(u, d, l, r, mode == Mode::DigitalNormalized);
}
```

测试：

- 四个方向。
- 对角归一化。
- 模拟量。

---

### 2.3.5 ActionState 内存布局

文件：`include/insys/actions/action_state.h`、`src/actions/action_state.cpp`。

按总体设计 11.3 定义结构体和 arena：

```cpp
struct ActionMapIndices {
    int actionStart, actionCount;
    int bindingStart, bindingCount;
    int controlStart, controlCount;
};

struct TriggerState { /* 见总体设计 */ };
struct BindingState { /* 见总体设计 */ };
struct InteractionState { /* 见总体设计 */ };

class ActionState : public IStateChangeMonitor {
public:
    void initialize(BindingResolver& resolver);
    void dispose();

    // 启用/禁用
    void enable_all(ActionMap& map);
    void disable_all(ActionMap& map);
    void enable_single(ActionDef& action);
    void disable_single(ActionDef& action);

    // 状态查询
    ActionPhase phase(const ActionDef& action) const;
    bool was_performed_this_frame(const ActionDef& action) const;
    bool was_pressed_this_frame(const ActionDef& action) const;
    bool was_released_this_frame(const ActionDef& action) const;

    template <typename T> T read_value(const ActionDef& action) const;

    // IStateChangeMonitor
    void notify_state_changed(Control* control, double time, Event* event,
                              int64_t monitorIndex) override;
    void notify_timer_expired(Control* control, double time,
                              int64_t monitorIndex, int timerIndex) override;

    // 供 Interaction 使用
    void change_phase_of_action(ActionPhase newPhase, TriggerState& trigger,
                                ActionPhase phaseAfterPerformedOrCanceled);
    void start_timeout(float seconds, TriggerState& trigger);
    void stop_timeout(int interactionIndex);

public:
    std::vector<ActionMap*> maps;
    std::vector<Control*> controls;
    std::vector<IInteraction*> interactions;
    std::vector<IProcessor*> processors;
    std::vector<IComposite*> composites;

    UnmanagedMemory memory;
    Manager* manager = nullptr;
};
```

`UnmanagedMemory` 用一个 `std::unique_ptr<uint8_t[]>` + 偏移切分。M2 可以用 `std::vector<uint8_t>`，但要保证指针稳定（一次性分配，不扩容）。

测试：

- 分配大小 = 各数组之和。
- 各指针偏移不重叠且对齐。

---

### 2.3.6 BindingResolver

文件：`include/insys/actions/binding_resolver.h`、`src/actions/binding_resolver.cpp`。

按总体设计 11.2 实现。

```cpp
class BindingResolver {
public:
    struct Result {
        std::vector<Control*> controls;
        std::vector<IInteraction*> interactions;
        std::vector<IProcessor*> processors;
        std::vector<IComposite*> composites;
        std::vector<ActionMap*> maps;
        UnmanagedMemory memory;
    };

    Result resolve(std::vector<ActionMap*> maps);
};
```

关键步骤：

1. 统计数量。
2. 分配 `UnmanagedMemory`。
3. 遍历 maps/actions/bindings：
   - composite binding：实例化 composite，记录 `compositeIndex`。
   - part binding：解析路径到控件，调用 `composite->assign_part(name, currentPartIndex)`。
   - 普通 binding：解析路径到控件。
   - 每个控件：`controls[controlIndex]`、`controlIndexToBindingIndex`、`BindingState.controlStart/count`。
   - 实例化 processors/interactions。
4. 填充 `actionBindingIndicesAndCounts` / `actionBindingIndices`。
5. 返回。

处理器/交互参数应用：

```cpp
void apply_parameters(IProcessor& p, const std::vector<NamedValue>& params);
void apply_parameters(IInteraction& i, const std::vector<NamedValue>& params);
```

由于 C++ 没有反射，处理器/交互需要自行实现 `set_parameter(name, value)`：

```cpp
class IInteraction {
public:
    virtual void process(InteractionContext&) = 0;
    virtual void reset() = 0;
    virtual bool set_parameter(std::string_view name, const PrimitiveValue& value) { return false; }
};
```

测试：

- 单绑定单控件。
- 一个绑定匹配多个控件。
- composite + parts。
- 处理器/交互参数应用。
- 数量统计正确。

---

### 2.3.7 启用/禁用

参考总体设计 11.9。

```cpp
void ActionState::enable_single(ActionDef& action) {
    if (action.actionIndex < 0) return;
    auto& ts = memory.actionStates[action.actionIndex];
    if (ts.phase != ActionPhase::Disabled) return;
    ts.phase = ActionPhase::Waiting;

    auto [start, count] = action_binding_range(action.actionIndex);
    for (int b = start; b < start + count; ++b) {
        auto& bs = memory.bindingStates[b];
        for (int c = 0; c < bs.controlCount; ++c) {
            set_control_enabled(bs.controlStartIndex + c, true);
            manager->add_state_change_monitor(
                controls[bs.controlStartIndex + c], this,
                pack_monitor_index(mapIndex, controlIndex, b),
                group_index);
        }
        if (wants_initial_state_check(bs)) bs.flags |= InitialStateCheckPending;
    }
}

void ActionState::disable_single(ActionDef& action) {
    // 取消进行中的 action，移除监视器，复位
}
```

注意：

- 同一控件被多个绑定使用时，`set_control_enabled` 需要引用计数；M2 可先用 `enabledControls` 位图 + 简单规则（任一绑定启用即启用）。
- 移除监视器时按 `monitor` + `monitorIndex` 精确移除。

测试：

- 启用后监视器数量正确。
- 禁用后不再收到回调。
- 重复启用/禁用幂等。

---

### 2.3.8 状态变化回调与 TriggerState

`pack_monitor_index` / `unpack_monitor_index`：

```cpp
int64_t pack(int mapIndex, int controlIndex, int bindingIndex) {
    return (int64_t(mapIndex) << 40) | (int64_t(controlIndex) << 16) | bindingIndex;
}
```

`notify_state_changed` 流程见总体设计 11.4。

M2 实现：

1. 解码 map/control/binding。
2. 构造 `TriggerState`。
3. 计算 magnitude。
4. 处理 composite（若 part）。
5. 处理按钮 press/release。
6. 冲突消解。
7. 默认交互或 interactions。
8. 相位变化 + 回调。

测试：

- 单控件 Button：按下 → Started + Performed；释放 → Canceled。
- Value：变化 → Started/Performed；回默认 → Canceled。
- PassThrough：每次变化 → Performed。

---

### 2.3.9 默认交互状态机

严格实现总体设计 11.4 的迁移表。

```cpp
void ActionState::process_default_interaction(TriggerState& trigger, int actionIndex) {
    auto& ts = memory.actionStates[actionIndex];
    auto& bs = memory.bindingStates[trigger.bindingIndex];

    switch (ts.phase) {
    case ActionPhase::Waiting:
        if (trigger.isPassThrough) {
            change_phase_of_action(ActionPhase::Performed, trigger, ActionPhase::Waiting);
        } else if (trigger.isButton) {
            if (trigger.magnitude > 0) change_phase_of_action(ActionPhase::Started, trigger, ActionPhase::Invalid);
            if (trigger.magnitude >= press_threshold(trigger)) {
                change_phase_of_action(ActionPhase::Performed, trigger, ActionPhase::Performed);
            }
        } else {
            if (is_actuated(trigger)) {
                change_phase_of_action(ActionPhase::Started, trigger, ActionPhase::Invalid);
                change_phase_of_action(ActionPhase::Performed, trigger, ActionPhase::Started);
            }
        }
        break;
    case ActionPhase::Started:
        // ...
    case ActionPhase::Performed:
        // ...
    }
}
```

`press_threshold`：

- 若绑定有 `PressInteraction` 且 `pressPoint > 0`，用它。
- 否则控件是 `ButtonControl` 时用其 `pressPoint`。
- 否则用全局默认。

测试：

- 覆盖迁移表每一项。

---

### 2.3.10 冲突消解

参考 `IsConflictingInput`。

M2 简化版：

```
if action.type == PassThrough: 不做冲突消解
if action 只有一个绑定且该绑定只匹配一个控件: 不做
否则:
   if action 未 Started:
       选择 magnitude 最大的控件
   else:
       若当前控件 magnitude 仍最大: 保持
       否则切换到 magnitude 更大的控件
```

需要维护 `controlMagnitudes`。

测试：

- 两控件同时按下，选择 magnitude 更大的。
- 当前控件释放、另一控件仍按下时切换。

---

### 2.3.11 初始状态检查

参考 `OnBeforeInitialUpdate`。

- 仅 `Value` 类型且 `initialStateCheck`（或 binding `wantsInitialStateCheck`）。
- 启用后第一次 Update 的 before 阶段：
  - 读取绑定控件当前值。
  - 若已 actuated，触发 Started + Performed。
  - 清除 `InitialStateCheckPending`。
- 必须避免与同一帧的状态事件重复触发。

实现：`Manager` 提供 `on_before_update` 回调；`ActionState` 注册。

测试：

- 启用时摇杆已偏移 → 立即 Started/Performed。
- 启用时按钮已按下 → 不立即触发（Button 不做初始检查）。

---

### 2.3.12 回调

```cpp
class Action {
public:
    std::function<void(const CallbackContext&)> started;
    std::function<void(const CallbackContext&)> performed;
    std::function<void(const CallbackContext&)> canceled;
};

struct CallbackContext {
    ActionPhase phase;
    Control* control;
    double time;
    double startTime;
    double duration() const { return time - startTime; }
    int valueSizeInBytes;
    void read_value(void* buffer, int size) const;
    template <typename T> T read_value() const;
    bool read_value_as_button() const;
};
```

`change_phase_of_action` 中：

- 更新 `TriggerState`。
- 调用对应回调。
- 回调期间设置 `m_inProcess = true`，禁止重新解析。

测试：

- 回调顺序与相位一致。
- 回调中读取 `control/value` 正确。

---

### 2.3.13 Demo 输入可视化

在 RmlUi 中显示：

- Action 名、类型、相位。
- 当前值（float/Vector2）。
- 当前驱动控件路径。
- 最近一次 started/performed/canceled 时间。

布局：

- `Move`（2DVector：WASD + 左摇杆）
- `Jump`（Button：Space + South）
- `Fire`（Button：LeftButton）

验收：

- 按键/摇杆变化时 UI 实时更新。

---

### 2.3.14 测试清单

- [x] `test_binding_resolver.cpp`
- [x] `test_action_state.cpp`（相位、冲突、初始检查、启用/禁用）
- [x] `test_processors.cpp`
- [x] `test_descriptor_parser.cpp`
- [x] 合成器由 `test_action_state.cpp` 的 Axis/Vector2 用例覆盖

---

## 2.4 验收标准

- [x] Value/Button/PassThrough 行为与总体设计一致。
- [x] 多绑定冲突消解正确。
- [x] 启用/禁用幂等，无悬空监视器。
- [x] 初始状态检查正确。
- [x] 处理器/合成器数值正确。
- [x] Demo 可视化正确。
- [x] `ctest` 全绿。

---

## 2.5 常见问题

1. **TriggerState 与 ActionDef 混淆**：运行时状态只在 `ActionState`，不要写回定义。
2. **监视器索引打包错误**：map/control/binding 位宽要够（参考 Unity 的打包方式）。
3. **回调中重新解析**：必须用 defer 机制，否则状态错乱。
4. **禁用时未取消进行中的 action**：会导致 UI 卡在 Performed。
5. **初始检查与状态事件重复触发**：检查后清 pending，并在状态事件回调中清对应 binding 的 pending。

---

## 2.6 下一步

进入 [M3-Interactions-Rebinding-Assets.md](M3-Interactions-Rebinding-Assets.md)。
