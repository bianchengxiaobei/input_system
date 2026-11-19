# M3 —— 交互 / 重绑定 / 自定义资产

> 前置：M2 完成。实现状态：M3 已完成（交互、重绑定、JSON 资产、ControlScheme 与 Demo 重绑定页）。
> 目标：实现 Interaction 体系（Tap/Hold/SlowTap/Press/MultiTap）、交互式重绑定、自定义 JSON 资产格式与加载、ControlScheme（简化版）。
> 参考：`../CppInputSystem_ImplementationPlan.md` 第 11.6、11.10、11.11、13 章。

---

## 3.1 目标与交付物

- `IInteraction` 接口与 `InteractionContext`。
- Tap / Hold / SlowTap / Press / MultiTap 五种交互。
- Interaction 超时机制（`start_timeout` / `stop_timeout`）。
- `RebindingOperation`：监听、过滤、评分、生成路径、应用覆盖。
- 自定义 JSON 资产：schema、加载、校验、可选烘焙。
- 简化版 ControlScheme。
- Demo：重绑定页面、交互演示。

---

## 3.2 任务清单

- [x] 3.2.1 InteractionContext
- [x] 3.2.2 Interaction 注册表与参数
- [x] 3.2.3 TapInteraction
- [x] 3.2.4 HoldInteraction
- [x] 3.2.5 SlowTapInteraction
- [x] 3.2.6 PressInteraction
- [x] 3.2.7 MultiTapInteraction
- [x] 3.2.8 Interaction 超时集成
- [x] 3.2.9 RebindingOperation
- [x] 3.2.10 绑定覆盖保存/加载
- [x] 3.2.11 自定义 JSON 资产
- [x] 3.2.12 ControlScheme（简化）
- [x] 3.2.13 Demo 重绑定页
- [x] 3.2.14 测试

---

## 3.3 步骤详解

### 3.3.1 InteractionContext

文件：`include/insys/actions/interaction.h`。

```cpp
class InteractionContext {
public:
    Action* action() const;
    Control* control() const;
    ActionPhase phase() const;
    double time() const;
    double startTime() const;
    bool timerHasExpired() const;

    float compute_magnitude();
    bool control_is_actuated(float threshold = 0.0f);

    void started();
    void performed();
    void performed_and_stay_started();
    void performed_and_stay_performed();
    void canceled();
    void waiting();

    void set_timeout(float seconds);
    void set_total_timeout_completion_time(float seconds);

    template <typename T> T read_value() const;
};
```

内部字段（参考 `InputInteractionContext.cs`）：

```cpp
ActionState* m_state;
TriggerState m_trigger;
bool m_timerExpired = false;
```

`started()` 等最终调用 `m_state->change_phase_of_interaction(..., m_trigger, ...)`。

测试：

- 上下文读取控件/时间正确。
- 相位方法映射到正确的 ActionState 调用。

---

### 3.3.2 Interaction 注册表与参数

```cpp
class InteractionRegistry {
public:
    using Factory = std::function<std::unique_ptr<IInteraction>()>;
    void add(std::string name, Factory factory);
    std::unique_ptr<IInteraction> create(std::string_view name);
    bool has(std::string_view name) const;
};
```

- 名称大小写不敏感。
- 参数通过 `set_parameter(name, value)` 设置；未知参数记录警告。
- 参数默认值来自交互对象的字段初始值。

```cpp
class IInteraction {
public:
    virtual ~IInteraction() = default;
    virtual void process(InteractionContext& ctx) = 0;
    virtual void reset() = 0;
    virtual bool set_parameter(std::string_view name, const PrimitiveValue& value) {
        return false;
    }
};
```

---

### 3.3.3 TapInteraction

参考 `TapInteraction.cs`（总体设计 11.6）。

```cpp
class TapInteraction : public IInteraction {
public:
    float duration = -1.0f;     // <=0 用 settings.defaultTapTime
    float pressPoint = -1.0f;   // <=0 用全局

    void process(InteractionContext& ctx) override {
        if (ctx.timerHasExpired()) {
            ctx.canceled();
            m_canceledFromTimer = true;
            return;
        }

        if (ctx.phase() == ActionPhase::Waiting &&
            ctx.control_is_actuated(press_point()) &&
            !m_canceledFromTimer) {
            m_tapStartTime = ctx.time();
            ctx.started();
            ctx.set_timeout(duration_or_default() + 0.00001f);
            return;
        }

        if (ctx.phase() == ActionPhase::Started &&
            !ctx.control_is_actuated(release_point())) {
            if (ctx.time() - m_tapStartTime <= duration_or_default()) {
                ctx.performed();
            } else {
                ctx.canceled();
            }
        }

        if (!ctx.control_is_actuated(release_point())) {
            m_canceledFromTimer = false;
        }
    }

    void reset() override { m_tapStartTime = 0; m_canceledFromTimer = false; }

private:
    double m_tapStartTime = 0;
    bool m_canceledFromTimer = false;
};
```

注意：

- 超时设置为 `duration + 0.00001`，保证“正好在边界释放”仍算有效。
- `canceledFromTimer` 防止模拟量控件在取消后立即重新开始。

测试：

- 短按 → Performed。
- 长按超过 duration → Canceled（超时）。
- 释放点使用 `releasePoint = pressPoint * releaseThreshold`。

---

### 3.3.4 HoldInteraction

参考 `HoldInteraction.cs`。

```cpp
class HoldInteraction : public IInteraction {
public:
    float duration = -1.0f;
    float pressPoint = -1.0f;

    void process(InteractionContext& ctx) override {
        if (ctx.timerHasExpired()) {
            ctx.performed_and_stay_performed();
            return;
        }
        switch (ctx.phase()) {
        case ActionPhase::Waiting:
            if (ctx.control_is_actuated(press_point())) {
                m_timePressed = ctx.time();
                ctx.started();
                ctx.set_timeout(duration_or_default());
            }
            break;
        case ActionPhase::Started:
            if (ctx.time() - m_timePressed >= duration_or_default())
                ctx.performed_and_stay_performed();
            if (!ctx.control_is_actuated())
                ctx.canceled();
            break;
        case ActionPhase::Performed:
            if (!ctx.control_is_actuated(press_point()))
                ctx.canceled();
            break;
        }
    }

    void reset() override { m_timePressed = 0; }

private:
    double m_timePressed = 0;
};
```

测试：

- 按住超过 duration → Performed 并保持。
- 提前释放 → Canceled。
- Performed 后释放 → Canceled。

---

### 3.3.5 SlowTapInteraction

参考 `SlowTapInteraction.cs`。

语义：

- 按下 → Started。
- 在 duration 之前释放 → Canceled。
- 达到 duration 后释放 → Performed。

实现要点：

```cpp
if (ctx.timerHasExpired()) { ctx.performed(); return; }  // 达到慢按时间
switch (phase) {
case Waiting:
    if (actuated(pressPoint)) { timePressed = time; started(); set_timeout(duration); }
    break;
case Started:
    if (!actuated(releasePoint)) {
        if (time - timePressed >= duration) performed();
        else canceled();
    }
    break;
}
```

测试：

- 短按 → Canceled。
- 长按后释放 → Performed。

---

### 3.3.6 PressInteraction

参考 `PressInteraction.cs`。

语义：按下和释放分别触发，支持三种行为：

```cpp
enum class PressBehavior {
    PressOnly,          // 按下时 Performed
    ReleaseOnly,        // 释放时 Performed
    PressAndRelease,    // 按下 Performed，释放再 Performed
};
```

参数：

- `behavior`：0/1/2。
- `pressPoint`。

实现要点：

- Waiting + actuated → Started；根据 behavior 决定是否 Performed。
- Started/Performed + 释放 → 根据 behavior 决定 Performed/Canceled。
- 释放后回 Waiting。

测试：

- 三种 behavior 的触发次数与相位。
- 模拟量阈值。

---

### 3.3.7 MultiTapInteraction

参考 `MultiTapInteraction.cs`。

参数：

- `tapCount`（默认 2）。
- `tapTime`（默认 `defaultTapTime`）。
- `tapDelay`（默认 `multiTapDelayTime`）。
- `pressPoint`。

语义：连续 `tapCount` 次 Tap，每次按下-释放间隔 ≤ tapTime，相邻两次间隔 ≤ tapDelay。

实现要点：

```
state: m_currentTapCount, m_currentTapStartTime

if timerExpired:
    if phase == Started: canceled()   // 超时
    return

switch phase:
  Waiting:
     if actuated(pressPoint):
        if m_currentTapCount == 0:
            started()
        m_currentTapStartTime = time
        if m_currentTapCount == 0:
            set_total_timeout_completion_time(tapTime * tapCount + (tapCount-1)*tapDelay)
        set_timeout(tapTime)
  Started:
     if !actuated(releasePoint):
        if time - m_currentTapStartTime <= tapTime:
            ++m_currentTapCount
            if m_currentTapCount >= tapCount:
                performed()
                m_currentTapCount = 0
                waiting()
            else:
                set_timeout(tapDelay)
        else:
            canceled()
            m_currentTapCount = 0
  ...
```

测试：

- 双击触发。
- 间隔过长不触发。
- 单击不触发。

---

### 3.3.8 Interaction 超时集成

`ActionState` 需要：

```cpp
void start_timeout(float seconds, TriggerState& trigger) {
    int interactionIndex = trigger.interactionIndex;
    auto& st = memory.interactionStates[interactionIndex];
    if (st.timerRunning) stop_timeout(interactionIndex);

    int64_t monitorIndex = pack(mapIndex, controlIndex, bindingIndex);
    manager->add_state_change_monitor_timeout(
        controls[trigger.controlIndex], this,
        trigger.time + seconds, monitorIndex, interactionIndex);

    st.timerRunning = true;
    st.timerStartTime = trigger.time;
    st.timerDuration = seconds;
    st.timerMonitorIndex = monitorIndex;
}

void stop_timeout(int interactionIndex) {
    auto& st = memory.interactionStates[interactionIndex];
    manager->remove_state_change_monitor_timeout(this, st.timerMonitorIndex, interactionIndex);
    st.timerRunning = false;
    st.totalTimeoutCompletionDone += st.timerDuration;
    st.totalTimeoutCompletionTimeRemaining =
        std::max(0.0f, st.totalTimeoutCompletionTimeRemaining - st.timerDuration);
    st.timerDuration = 0;
}

void notify_timer_expired(Control* control, double time,
                          int64_t monitorIndex, int timerIndex) override {
    auto& st = memory.interactionStates[timerIndex];
    InteractionContext ctx{this, make_trigger(st, time), /*timerExpired=*/true};
    st.timerRunning = false;
    interactions[timerIndex]->process(ctx);
}
```

测试：

- 超时回调进入正确的 interaction。
- 取消/重新设置超时时旧超时被移除。

---

### 3.3.9 RebindingOperation

参考 `InputActionRebindingExtensions.cs`。

接口（总体设计 11.10）：

```cpp
class RebindingOperation {
public:
    RebindingOperation& with_action(ActionDef& action);
    RebindingOperation& with_target_binding(int bindingIndex);
    RebindingOperation& with_expected_control_type(std::string_view layout);
    RebindingOperation& with_controls_excluding(std::string_view path);
    RebindingOperation& with_controls_having_to_match_path(std::string_view path);
    RebindingOperation& with_magnitude_greater_than(float magnitude);
    RebindingOperation& with_timeout(float seconds);
    RebindingOperation& with_matching_events_suppressed(bool value = true);

    RebindingOperation& on_match(std::function<void(Control*)>);
    RebindingOperation& on_cancel(std::function<void()>);
    RebindingOperation& on_complete(std::function<void()>);

    RebindingOperation& start();
    void cancel();
    void dispose();
};
```

流程：

1. `start()`：注册 `System::on_event` 监听。
2. 对每个事件：
   - 找到设备与控件。
   - 过滤：噪声控件、鼠标位置、排除路径、不匹配路径。
   - 评分：`control.magnitude` / 事件类型。
   - 若达到阈值，记录候选。
3. 超时：`m_timeout` 秒后取消。
4. `on_match`：生成路径。
5. `on_complete`：应用 `binding.overridePath = path`，重新解析。

路径生成：

```cpp
std::string generate_path(Control* control) {
    // 例如 "Keyboard/Space" 或 "Gamepad/ButtonSouth"
    return control->device()->layout + "/" + control->path_suffix();
}
```

- 默认不泛化（具体路径）。
- 可提供 `without_generalizing_path()` / `with_generalizing_path()` 选项。

应用覆盖：

```cpp
actionMap.bindings[bindingIndex].overridePath = path;
actionState->prepare_for_binding_resolution();
actionState->finish_binding_resolution();
```

测试：

- 监听并捕获正确控件。
- 排除路径生效。
- 超时取消。
- 应用后新路径生效。

---

### 3.3.10 绑定覆盖保存/加载

参考 `InputActionMap` 的 `BindingOverrideJson`。

```json
{
  "bindings": [
    {
      "action": "Player/Jump",
      "id": "xxxxxxxx-...",
      "path": "Keyboard/Space",
      "interactions": "tap",
      "processors": ""
    }
  ]
}
```

- 保存：遍历所有 binding，输出 `overridePath/overrideInteractions/overrideProcessors`。
- 加载：按 `id` 或 `action` 匹配 binding，写回 override。
- 只保存覆盖值，不修改原始资产。

测试：

- 保存/加载往返一致。
- 找不到 binding 时忽略并记录。

---

### 3.3.11 自定义 JSON 资产

按总体设计第 13 章实现。

文件：

- `include/insys/asset/asset_loader.h`
- `src/asset/asset_loader.cpp`

```cpp
class AssetLoader {
public:
    static ActionAsset load_from_file(const std::filesystem::path& path);
    static ActionAsset load_from_json(std::string_view json);
    static std::string to_json(const ActionAsset& asset);
};
```

校验规则：

1. `version` 必须是已知版本（当前 1）。
2. map/action 名唯一（大小写不敏感）。
3. `binding.action` 必须存在。
4. composite 类型必须已注册。
5. part 名必须能被 composite 识别（可延迟到 resolve）。
6. path 语法合法（可延迟到 resolve，但 malformed 路径要报错）。

错误处理：

- 返回 `Result<ActionAsset>` 或抛出 `AssetError`（含路径 + 行号 + 原因）。
- 不要静默忽略字段拼写错误。

测试：

- 合法资产加载。
- 缺失字段/重复名/未知 composite/非法路径的错误信息。
- 往返序列化。

---

### 3.3.12 ControlScheme（简化）

```cpp
struct ControlScheme {
    std::string name;
    std::string bindingGroup;
    std::vector<std::string> devices;   // 布局名
    bool is_optional = false;
};

bool scheme_matches(const ControlScheme& scheme,
                    const std::vector<Device*>& devices);
```

- 简单判断：每个必需 `device` 布局都至少有一个连接设备。
- 根据 scheme 的 `bindingGroup` 过滤 binding groups（M3 可只做查询，不做自动切换）。

测试：

- 键鼠 scheme / 手柄 scheme。
- 缺设备时不匹配。

---

### 3.3.13 Demo 重绑定页

RmlUi 页面：

- 列出 `Jump`、`Fire` 的当前绑定。
- “Rebind”按钮：点击后进入监听状态，提示“按下新按键”。
- 显示新绑定，并可保存到本地 override 文件。
- “Reset”按钮：清除 override。

验收：

- 重绑定后按新键触发 action。
- 保存后重启 Demo 仍生效（加载 override）。

---

### 3.3.14 测试清单

- [x] `test_interactions.cpp`（Tap/Hold/SlowTap/Press/MultiTap + 超时）
- [x] `test_rebinding.cpp`
- [x] `test_binding_override.cpp`
- [x] `test_asset_loader.cpp`（含 JSON 校验、往返、ControlScheme）

---

## 3.4 验收标准

- [x] 五种交互时序正确。
- [x] 超时机制正确，无残留 timer。
- [x] 重绑定后新路径立即生效，可保存/加载。
- [x] 自定义资产加载/校验/序列化正确。
- [x] ControlScheme 判断正确。
- [x] Demo 重绑定页可用。
- [x] `ctest` 全绿。

---

### 3.4.1 实现说明

- `SlowTap` 遵循 Unity 原实现：不安装 timer，按下后 Started，释放时根据已按住时长决定 Performed/Canceled。
- 超时通过 `Manager` 的 state-change timeout 队列触发；interaction phase 变化和 action 禁用都会移除旧 timer。
- JSON 解析/序列化为项目内轻量实现（`insys/asset/json.h`），不引入第三方 JSON 依赖；加载器对未知字段、重复名、未知 composite/interaction/processor、非法路径报错。
- 重绑定在输入事件处理中捕获控件、写入 `overridePath` 并立即 `ActionState::re_resolve()`；匹配事件默认标记 handled，避免同一次按下同时触发 action。
- binding override 只读写 `overridePath/overrideInteractions/overrideProcessors`，不修改原始 binding 字段。
- 使用 `ActionAsset`/`ActionMap` 时需保证 `Manager` 生命周期长于已启用资产（Demo 已按此顺序声明）。

## 3.5 常见问题

1. **超时未清理**：`stop_timeout` 必须从 Manager 移除，否则会收到过期 timer。
2. **Tap 边界**：超时设置 `duration + 0.00001`，避免边界释放被判定为长按。
3. **MultiTap 状态泄漏**：取消/完成后必须重置 `m_currentTapCount`。
4. **重绑定期间事件被游戏消费**：使用 `with_matching_events_suppressed` 抑制。
5. **覆盖保存覆盖原始资产**：只写 override 字段。

---

## 3.6 下一步

进入 [M4-RmlUi-Integration.md](M4-RmlUi-Integration.md)。
