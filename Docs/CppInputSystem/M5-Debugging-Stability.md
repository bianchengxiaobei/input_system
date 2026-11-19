# M5 —— 调试工具与稳定性

> 前置：M4 完成。
> 目标：用 RmlUi 实现输入调试器；实现事件 Trace/回放；补齐性能优化、内存检查、稳定性测试。
> 参考：`../CppInputSystem_ImplementationPlan.md` 第 14、16 章。

---

## 5.1 目标与交付物

- RmlUi Debugger：设备、控件、事件流、Action 状态、绑定信息。
- 事件 Trace：录制/保存/加载/回放。
- 事件监视 API：`System::on_event`、`System::on_device_change`、`System::on_action_change`。
- 性能：事件处理与读取路径零堆分配（稳态）。
- 内存：ASan/Valgrind 无泄漏；长时间运行稳定。
- 测试覆盖率补齐。

---

## 5.2 任务清单

- [x] 5.2.1 事件监视 API
- [x] 5.2.2 Event Trace 录制
- [x] 5.2.3 Event Trace 回放
- [x] 5.2.4 Debugger 数据模型
- [x] 5.2.5 Debugger RmlUi 页面
- [x] 5.2.6 Action 状态面板
- [x] 5.2.7 绑定/重绑定面板
- [x] 5.2.8 性能优化
- [x] 5.2.9 内存检查（生命周期/稳定性测试；Windows ASan 已通过，Valgrind 未执行）
- [x] 5.2.10 稳定性测试
- [x] 5.2.11 覆盖率与文档（测试矩阵与文档已补齐，覆盖率百分比未在本机统计）

---

## 5.3 步骤详解

### 5.3.1 事件监视 API

参考 `InputSystem.onEvent`、`onDeviceChange`、`onActionChange`。

```cpp
class System {
public:
    using EventListener = std::function<void(const Event&, Device*)>;
    using DeviceChangeListener = std::function<void(Device*, DeviceChange)>;
    using ActionChangeListener = std::function<void(Action*, ActionChange)>;

    int add_event_listener(EventListener);
    void remove_event_listener(int id);
    int add_device_change_listener(DeviceChangeListener);
    void remove_device_change_listener(int id);
    int add_action_change_listener(ActionChangeListener);
    void remove_action_change_listener(int id);
};
```

- 监听器在事件处理前调用（与 Unity 一致）。
- 监听器可设置 `event.handled`；按策略决定是否跳过后续处理。
- 监听器数量为 0 时走无回调快速路径。

测试：

- 事件监听收到顺序正确。
- 监听器里修改 handled 生效。
- 移除监听器后不再收到。

---

### 5.3.2 Event Trace 录制

参考 `InputEventTrace`。

```cpp
class EventTrace {
public:
    struct Options {
        size_t maxBufferBytes = 4 * 1024 * 1024;
        bool recordDeviceChanges = true;
    };

    explicit EventTrace(Options options = {});
    void enable();          // 开始录制
    void disable();
    void clear();

    // 保存/加载（自定义二进制）
    bool save(const std::filesystem::path& path) const;
    bool load(const std::filesystem::path& path);

    // 回放：把事件注入 Manager（按时间排序）
    void replay(Manager& manager);
    void replay(Manager& manager, double speed = 1.0);

    size_t event_count() const;
    const std::vector<uint8_t>& raw() const;
};
```

实现：

- `enable()` 时注册 `System::add_event_listener`，把事件按对齐格式追加到内部缓冲。
- 缓冲达到上限时循环覆盖或停止（可配置）。
- 保存格式：

```
[magic "INSYTRCE"][version][eventCount][totalBytes]
[event0][event1]...
```

- 回放时解析事件并调用 `manager.queue_event`，然后按事件时间戳驱动 `manager.update`。

测试：

- 录制后事件数正确。
- 保存/加载往返一致。
- 回放后设备状态与录制结束时一致。

---

### 5.3.3 Event Trace 回放

回放需要可控时间：

```cpp
void EventTrace::replay(Manager& manager, double speed) {
    double start = m_events.front().time;
    for (auto& e : m_events) {
        double target = start + (e.time - start) / speed;
        // 宿主循环中推进时间，或在测试中直接按事件时间 Update
        manager.queue_event(&e);
        manager.update(UpdateType::Dynamic);
    }
}
```

- 测试中可直接按事件逐条注入，不依赖真实时间。
- Demo 中回放可暂停/单步/调速。

验收：

- 回放录制的键鼠序列，RmlUi 状态与录制时一致。

---

### 5.3.4 Debugger 数据模型

```cpp
struct DebuggerModel {
    struct DeviceInfo {
        int deviceId;
        std::string name;
        std::string layout;
        bool enabled;
        bool connected;
        std::vector<ControlInfo> controls;
    };
    struct ControlInfo {
        std::string path;
        std::string layout;
        std::string value;
        uint32_t offset;
        uint32_t sizeInBits;
    };
    struct ActionInfo {
        std::string map;
        std::string name;
        ActionPhase phase;
        std::string value;
        std::string drivingControl;
        double lastPerformedTime;
    };
    struct EventInfo {
        double time;
        std::string type;
        int deviceId;
        uint32_t size;
    };

    std::vector<DeviceInfo> devices;
    std::vector<ActionInfo> actions;
    std::deque<EventInfo> recentEvents;   // 最近 N 条
};
```

- 由 `System` 每帧（或按需）刷新。
- 只读快照，避免调试器影响运行时。

---

### 5.3.5 Debugger RmlUi 页面

页面结构：

```
tools/debugger.rml
  <div id="devices">       设备列表 + 展开控件
  <div id="actions">       Action 状态表
  <div id="events">        事件流
  <div id="binding">       绑定与重绑定
```

- 使用 data binding（`data-model`）绑定 `DebuggerModel`。
- 控件值用文本框/进度条显示。
- 事件流自动滚动。
- 提供开关：暂停刷新、过滤设备、只显示变化值。

实现方式：

- Demo 中把 `DebuggerModel` 包装成 RmlUi `Rml::DataModel`（4.x）或手动更新 DOM。
- 若 RmlUi 版本不支持 data binding，则每帧 `SetInnerRML` 重建列表（仅调试工具可接受）。

验收：

- 设备插拔实时反映。
- 按键/摇杆值实时变化。
- Action 相位实时变化。

---

### 5.3.6 Action 状态面板

显示：

- map/action 名。
- 相位（Disabled/Waiting/Started/Performed/Canceled）。
- 当前值。
- 当前驱动控件路径。
- 最近 started/performed/canceled 时间与帧号。

可用颜色区分相位：

- Waiting：灰
- Started：黄
- Performed：绿
- Canceled：红

---

### 5.3.7 绑定/重绑定面板

- 列出每个 action 的绑定路径、interactions、processors。
- 显示 override 状态。
- “Rebind”按钮触发 `RebindingOperation`。
- “Reset”清除 override。
- 保存/加载 override 文件。

---

### 5.3.8 性能优化

重点：

1. **事件处理零分配**：
   - `EventQueue::drain` 使用交换缓冲，不每帧新建 vector。
   - `EventStream` 预分配 append 缓冲。
   - 监听器数组使用小数组内联/预分配，避免每帧分配。
2. **读取路径**：
   - `Control::read_value` 缓存原始值与处理值；状态未变时不重复计算。
   - 处理器 `CachingPolicy::CacheResult` 生效。
3. **监视器**：
   - `process_state_change` 只在有监视器的设备上执行。
   - 信号位图使用 `uint32_t` 数组，避免 `vector<bool>` 位代理开销。
4. **ActionState**：
   - 单块 arena 一次性分配。
   - 避免在热路径使用 `std::function`（可用函数指针 + void* 或内联小函数）。
5. **字符串**：
   - 控件名/路径使用 `InternedString`（整数 ID），避免热路径字符串比较。
6. **Profile**：
   - 可选接入 Tracy，在 `update`、`process_event_buffer`、`update_state`、`notify_state_changed` 打点。

基准：

- 1000 事件/帧处理耗时 < 1ms（i5 级别）。
- 100 actions × 10 bindings 启用后，空闲帧 < 0.2ms。
- 稳态每帧堆分配 = 0（用计数器验证）。

---

### 5.3.9 内存检查

- Windows：`/fsanitize=address`（MSVC 17.4+）或 Dr. Memory。
- Linux/macOS：ASan + LSan、Valgrind。
- 检查项：
  - 事件缓冲、状态缓冲、arena 释放。
  - ActionState dispose 后无悬空监视器。
  - 设备移除后监视器/超时清理。
  - rebinding 取消后监听器移除。

测试：

- 反复添加/移除 1000 次设备，内存不增长。
- 反复启用/禁用 1000 次 action，监视器数量回到 0。
- 长时间运行（1 小时）无泄漏。

---

### 5.3.10 稳定性测试

- 随机事件模糊测试：随机设备/控件/时间戳/大小，确保不崩溃。
- 非法事件：size 越界、未知类型、错误 format、未知 deviceId。
- 设备热插拔风暴。
- Action 回调中修改 action（启用/禁用/重绑定）。
- RmlUi 高 DPI 切换、窗口最小化/恢复、失焦。

---

### 5.3.11 覆盖率与文档

- 目标：核心模块行覆盖 ≥ 80%。
- 每个公共接口至少一个测试。
- 更新 README 进度表与已知问题。

---

## 5.4 验收标准

- [x] Debugger 能实时显示设备/控件/事件/Action。
- [x] Trace 录制/保存/加载/回放正确。
- [x] 稳态零堆分配。
- [x] ASan/Valgrind 无错误（Windows/MSVC `/fsanitize=address` 核心 `insys_tests` 94 用例 / 1538 断言通过；Demo/UI 未在 ASan 下运行）。
- [x] 模糊测试/热插拔风暴无崩溃。
- [ ] 覆盖率达标（未用覆盖率工具统计；公共接口均有测试）。

---

## 5.5 常见问题

1. **Debugger 影响运行时**：调试器只读快照，不要在事件回调里直接刷新 UI。
2. **Trace 缓冲无限增长**：必须设上限。
3. **回放时间不准**：使用事件时间戳而非真实时间。
4. **监听器泄漏**：移除设备/action 时清理监听器。
5. **std::function 热路径分配**：改用函数指针或小对象优化。

---

## 5.6 实现记录（M5）

- 事件监视：实际 API 为 `Manager::add_event_listener` / `add_device_change_listener` /
  `add_action_change_listener`，`System` 提供同名转发；监听器 ID 用于移除。公共
  event listener 在状态更新/派发前调用，`low::set_event_handled()` 可阻止后续处理。
  无监听器时走空 vector 快速路径。
- DeviceChange：`Added` / `Removed` / `ConfigurationChanged` / `SoftReset` /
  `HardReset` / `Disconnected` 已接入；移除设备时在销毁设备对象前通知。
- ActionChange：启停、Map 启停、Started/Performed/Canceled 以及
  BoundControlsAboutToChange/Changed 已接入 `ActionState`。
- EventTrace：`EventTraceOptions{maxBufferBytes, recordDeviceChanges, stopWhenFull}`；
  `INSYTRCE` 二进制格式；设备变更以内部 `DCHG` 事件保存，回放时跳过；
  `replay()` 当前采用确定性的逐事件注入（`speed` 保留在 API 中，宿主可配合
  `replay_next()` 自行控制节奏）。
- Debugger：`DebuggerModel` 每帧生成只读快照，Demo 手工更新 DOM（未使用 RmlUi
  data binding），包含设备/控件、Action 相位/值/驱动控件、最近事件、绑定与 override；Demo 同时监听 ActionChange 以记录相位时间。
- 性能：`Manager::update` 改为直接复用 native 字节 vector，不再每帧构造
  `EventBuffer`/`EventStream`；`EventQueue` 保留 recycle 缓冲；StateChangeMonitors
  使用预分配 scratch。`test_performance.cpp` 验证 1000 次纯事件更新以及带 Action
  的更新稳态为 0 次堆分配。
- 测试文件：`test_listeners_trace.cpp`、`test_debugger_model.cpp`、`test_performance.cpp`、`test_stability.cpp`。
- 稳定性：新增非法 size/device/delta 边界校验、随机事件模糊测试、1000 次设备热插拔、
  1000 次 Action 启停、Action 回调内禁用自身测试。
- 当前测试：`insys_tests` 94 用例 / 1538 断言，`insys_ui_tests` 15 用例 / 133 断言。
- ASan：Windows/MSVC `/fsanitize=address` 构建的核心 insys_tests 已通过（94 用例 / 1538 断言）；Demo/UI 与 Valgrind 未执行。
- 未完成：覆盖率百分比统计、真实手柄热插拔与 DPI 人工验收、IME。

## 5.7 下一步

进入 [M6-Enhancements.md](M6-Enhancements.md)。
