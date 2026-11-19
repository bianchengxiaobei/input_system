# C++ 重写 Unity Input System —— 详细实施文档

> 目标读者：负责按本文档一步步实现的工程师。
> 参考实现：`Packages/com.unity.inputsystem`（Input System 1.20.2，Unity 6000）。
> 本文档只描述“如何用 C++ 重写”，不追求与 Unity 二进制/资产格式兼容。
> 文中所有“参考原文件”均指向本仓库内路径，便于对照阅读。

> **按里程碑拆分的逐步实施文档见 [`CppInputSystem/README.md`](CppInputSystem/README.md)**：
> M0 核心骨架、M1 布局/手柄、M2 Actions 基础、M3 交互/重绑定/资产、M4 RmlUi、M5 调试/稳定性、M6 增强。
> 本文档保留为总体设计与算法参考。

---

## 0. 文档说明

- 本文档按“从底层到上层”的顺序组织，模块之间有明确依赖方向，建议严格按里程碑顺序实现。
- 每个模块都给出：职责、数据结构、关键算法、C++ 接口草案、参考原文件、验收点。
- 代码片段是接口草案，允许在实现时调整命名，但**语义必须保持一致**。
- 统一命名空间：`insys`（核心），`insys::low`（事件/状态），`insys::backend`（平台后端），`insys::ui`（RmlUi 适配）。
- 统一使用 C++20，构建系统 CMake。

---

## 1. 目标、范围与非目标

### 1.1 目标

1. 用 C++ 重写 Unity Input System 的核心能力：
   - 设备（Device）
   - 控件（Control）
   - 布局（Layout）
   - 状态（State）
   - 事件（Event）
   - 动作（Action / Binding / Interaction / Composite / Processor）
   - 交互式重绑定（Rebinding）
2. Windows 上使用 **GLFW** 采集键盘、鼠标、手柄输入。
3. 把输入接入 **RmlUi**，同时服务游戏正式 UI 与调试工具。
4. 提供一个带窗口的**验收 Demo**，用于人工/自动化验收。

### 1.2 首版范围

- 设备：Keyboard、Mouse、Gamepad、Joystick。
- 更新类型：Dynamic、Fixed、BeforeRender（可选 Manual）。
- Actions：Value / Button / PassThrough；Tap / Hold / SlowTap / Press / MultiTap；1D/2D/3D 轴、2D/3D 向量、单/双修饰键 Composite；常用 Processor。
- 资产：自定义 JSON 格式（见第 14 节），可选烘焙为二进制。
- UI：RmlUi 指针、键盘、文本、导航（手柄）输入。
- 平台：Windows（接口保留跨平台扩展位）。

### 1.3 非目标（首版不做）

- 不实现 Unity 的 `.inputactions` 兼容。
- 不实现任何渲染后端（RmlUi 的 `RenderInterface` 由宿主/Demo 提供）。
- 不实现 XR、Steam、HID 原始设备、触屏、传感器。
- 不实现 Unity 编辑器工具（Asset Editor、Debugger 窗口等）；M8 提供独立 Web 编辑器，不集成 Unity。
- 不实现 Unity 的 PlayerLoop 注入、ScriptableObject、序列化系统。

---

## 2. 术语与命名对照

| Unity | C++（本文档） | 说明 |
|---|---|---|
| `InputSystem` | `insys::System` | 公共门面（静态/单例） |
| `InputManager` | `insys::Manager` | 核心枢纽 |
| `InputDevice` | `insys::Device` | 设备 |
| `InputControl` | `insys::Control` | 控件基类 |
| `InputControl<T>` | `insys::ControlT<T>` | 类型化控件 |
| `InputControlLayout` | `insys::Layout` | 布局 |
| `InputStateBlock` | `insys::StateBlock` | 状态块 |
| `InputStateBuffers` | `insys::StateBuffers` | 状态缓冲 |
| `InputEvent` | `insys::low::Event` | 事件头 |
| `StateEvent` | `insys::low::StateEvent` | 全量状态事件 |
| `DeltaStateEvent` | `insys::low::DeltaStateEvent` | 增量状态事件 |
| `InputAction` | `insys::Action` | 动作 |
| `InputActionMap` | `insys::ActionMap` | 动作表 |
| `InputActionAsset` | `insys::ActionAsset` | 动作资产 |
| `InputBinding` | `insys::Binding` | 绑定 |
| `InputActionState` | `insys::ActionState` | 动作执行状态 |
| `InputBindingResolver` | `insys::BindingResolver` | 绑定解析器 |
| `IInputInteraction` | `insys::IInteraction` | 交互 |
| `InputBindingComposite` | `insys::IComposite` | 合成器 |
| `InputProcessor` | `insys::IProcessor` | 处理器 |
| `IInputRuntime` | `insys::IRuntime` | 运行时抽象 |
| `NativeInputSystem` | `insys::backend::IPlatformBackend` | 平台后端 |
| `InputSystemUIInputModule` | `insys::ui::RmlUiInputAdapter` | UI 输入适配 |

---

## 3. 总体架构

```
┌───────────────────────────────────────────────────────────────┐
│ 宿主程序                                                       │
│   Rml::Context + RenderInterface（宿主负责渲染）               │
│   ↑ insys::ui::RmlUiInputAdapter（本库提供）                   │
├───────────────────────────────────────────────────────────────┤
│ Actions 层                                                     │
│   ActionAsset → ActionMap → Action → Binding                   │
│   ActionState（SoA 非托管状态）                                │
│   BindingResolver / Interaction / Composite / Processor        │
├───────────────────────────────────────────────────────────────┤
│ 设备/控件层                                                     │
│   Device → ControlT<T> 树；Layout + DeviceBuilder              │
│   ControlPath 解析/匹配；StateBlock                             │
├───────────────────────────────────────────────────────────────┤
│ 事件/状态层                                                     │
│   Event / StateEvent / DeltaStateEvent / TextEvent             │
│   EventQueue / EventBuffer / EventStream                       │
│   StateBuffers / StateChangeMonitor                            │
├───────────────────────────────────────────────────────────────┤
│ 核心                                                           │
│   insys::Manager（设备注册、事件分发、更新调度、命令）          │
├───────────────────────────────────────────────────────────────┤
│ 运行时抽象                                                     │
│   IRuntime / IPlatformBackend / IWindow / ITime / IScheduler    │
├───────────────────────────────────────────────────────────────┤
│ 后端实现                                                       │
│   GlfwBackend（键盘/鼠标/手柄，唯一安装 GLFW 回调者）           │
└───────────────────────────────────────────────────────────────┘
```

依赖方向只能是**自上而下**：Actions 依赖 Device/State/Event；Device 依赖 State/Event；Event/State 不依赖上层；Manager 统领所有运行时对象；backend 只向上产事件。

---

## 4. 目录结构与构建

```
insys/
  CMakeLists.txt
  include/insys/
    System.h
    Manager.h
    UpdateType.h
    settings.h
    device.h
    control.h
    control_layout.h
    control_path.h
    state_block.h
    state_buffers.h
    low/
      event.h
      event_queue.h
      event_buffer.h
      event_stream.h
      fourcc.h
    actions/
      action.h
      action_map.h
      action_asset.h
      action_state.h
      binding.h
      binding_resolver.h
      interaction.h
      composite.h
      processor.h
      rebinding.h
      control_scheme.h
    backend/
      iruntime.h
      iplatform_backend.h
      iwindow.h
      itime.h
      ischeduler.h
    ui/
      rmlui_input_adapter.h
      rmlui_system_interface.h
    asset/
      asset_loader.h
  src/
    core/
      system.cpp
      manager.cpp
      device.cpp
      control.cpp
      control_layout.cpp
      control_path.cpp
      device_builder.cpp
      state_block.cpp
      state_buffers.cpp
      event.cpp
      event_queue.cpp
      event_buffer.cpp
      event_stream.cpp
    actions/
      action.cpp
      action_map.cpp
      action_asset.cpp
      action_state.cpp
      binding_resolver.cpp
      interactions.cpp
      composites.cpp
      processors.cpp
      rebinding.cpp
    backend/glfw/
      glfw_backend.cpp
      glfw_key_map.cpp
      glfw_gamepad_map.cpp
    ui/rmlui/
      rmlui_input_adapter.cpp
      rmlui_system_interface.cpp
    asset/
      asset_loader.cpp
  samples/acceptance/
    main.cpp
    renderer_gl3.*
    assets/ui/*.rml
    assets/ui/*.rcss
    assets/input/*.json
    acceptance.md
  tests/
    test_fourcc.cpp
    test_state_block.cpp
    test_layout_builder.cpp
    test_control_path.cpp
    test_action_state.cpp
    test_interactions.cpp
```

构建：

```
cmake -S insys -B build -DINSYS_BUILD_TESTS=ON -DINSYS_BUILD_DEMO=ON
cmake --build build --config Release
```

依赖：

| 依赖 | 用途 | 是否进核心库 |
|---|---|---|
| GLFW 3.4+ | 窗口/输入后端 | 否（仅 backend） |
| nlohmann/json | 资产解析 | 是（asset） |
| RmlUi | UI 输入适配（仅头文件/接口） | 否（ui 模块可选） |
| Catch2/GoogleTest | 单测 | 否（tests） |
| glad + OpenGL 3.3 | 仅 Demo 渲染 | 否（samples） |

> 核心库 `insys` 不依赖 GLFW、RmlUi、OpenGL。它们只被 backend/ui/samples 使用。

---

## 5. 基础层

### 5.1 FourCC

参考：`InputSystem/Runtime/Events/InputEvent.cs`、`InputSystem/Runtime/Utilities/FourCC.cs`。

```cpp
struct FourCC {
    uint32_t value = 0;
    constexpr FourCC() = default;
    constexpr explicit FourCC(uint32_t v) : value(v) {}
    constexpr FourCC(char a, char b, char c, char d = ' ')
        : value((uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
                (uint32_t(uint8_t(c)) << 8)  |  uint32_t(uint8_t(d))) {}
    constexpr bool operator==(FourCC o) const { return value == o.value; }
    constexpr bool operator!=(FourCC o) const { return value != o.value; }
};
```

内置格式常量（对应 `InputStateBlock`）：

| 常量 | FourCC | 位数 |
|---|---|---|
| `FormatBit` | `BIT ` | 1 |
| `FormatSBit` | `SBIT` | 1 |
| `FormatInt` | `INT ` | 32 |
| `FormatUInt` | `UINT` | 32 |
| `FormatShort` | `SHRT` | 16 |
| `FormatUShort` | `USHT` | 16 |
| `FormatByte` | `BYTE` | 8 |
| `FormatSByte` | `SBYT` | 8 |
| `FormatLong` | `LNG ` | 64 |
| `FormatULong` | `ULNG` | 64 |
| `FormatFloat` | `FLT ` | 32 |
| `FormatDouble` | `DBL ` | 64 |
| `FormatVector2` | `VEC2` | 64 |
| `FormatVector3` | `VEC3` | 96 |
| `FormatQuaternion` | `QUAT` | 128 |

### 5.2 内存与容器

- 状态/事件使用连续 `uint8_t` 缓冲 + 手动偏移，避免对象开销。
- 控件树使用 `std::vector<std::unique_ptr<Control>>` 持有，`Device` 内维护 `allControls` 指针数组。
- Actions 状态使用单块 arena（见 12.4）。
- 提供 `align_up(n, a)`、`bits_to_bytes()` 等工具。

```cpp
inline uint32_t align_up(uint32_t n, uint32_t a) { return (n + a - 1) & ~(a - 1); }
inline uint32_t bits_to_bytes(uint32_t bits) { return (bits + 7) >> 3; }
```

### 5.3 更新类型

参考：`InputSystem/Runtime/InputUpdateType.cs`。

```cpp
enum class UpdateType : uint32_t {
    None = 0,
    Dynamic = 1u << 0,
    Fixed = 1u << 1,
    BeforeRender = 1u << 2,
    Manual = 1u << 3,
    Default = Dynamic | Fixed,
};
using UpdateMask = uint32_t;
```

- `Manager::Update(UpdateType)` 每次只处理一种。
- 每种类型有独立的双缓冲（见 8.2）。
- `BeforeRender` 只处理开启了 `updateBeforeRender` 的设备。
- `Manual` 供测试/特殊宿主手动驱动。

### 5.4 设置项与默认值

参考：`InputSystem/Runtime/InputSettings.cs`。

```cpp
struct Settings {
    float defaultDeadzoneMin = 0.125f;
    float defaultDeadzoneMax = 0.925f;
    float defaultButtonPressPoint = 0.5f;
    float buttonReleaseThreshold = 0.75f;   // 相对 pressPoint
    float defaultTapTime = 0.2f;
    float defaultSlowTapTime = 0.5f;
    float defaultHoldTime = 0.4f;
    float tapRadius = 5.0f;
    float multiTapDelayTime = 0.75f;
    int   maxQueuedEventsPerUpdate = 1000;
    int   maxEventBytesPerUpdate = 5 * 1024 * 1024;
    float pollingFrequency = 60.0f;
    bool  disableRedundantEventsMerging = false;
    UpdateMask updateMask = UpdateType::Dynamic | UpdateType::Fixed;
};
```

---

## 6. 事件层

### 6.1 事件头布局

参考：`InputSystem/Runtime/LowLevel/InputEvent.cs`（原实现为与原生 ABI 兼容，字段顺序：type、sizeInBytes、deviceId、time、eventId）。

本实现不要求与 Unity 二进制兼容，采用固定 24 字节头，便于 8 字节对齐：

```cpp
namespace insys::low {

constexpr uint32_t kEventAlignment = 8;
constexpr uint32_t kHeaderSize = 24;      // 24 字节，8 字节对齐
constexpr uint16_t kMaxEventSize = 0xFFFF;
constexpr int kInvalidEventId = 0;

struct Event {
    uint32_t type;        // FourCC
    uint16_t size;        // 含头部的总字节数，4 的倍数
    uint16_t deviceId;
    uint32_t eventId;     // bit31 = handled
    uint32_t _pad0;       // 保留，保证 8 字节对齐
    double   time;        // 输入时间（秒）
};

static_assert(sizeof(Event) == kHeaderSize);

inline bool is_handled(const Event& e) { return (e.eventId & 0x80000000u) != 0; }
inline void set_handled(Event& e, bool v) {
    if (v) e.eventId |= 0x80000000u; else e.eventId &= 0x7FFFFFFFu;
}
inline int event_id(const Event& e) { return int(e.eventId & 0x7FFFFFFFu); }

inline Event* next_in_memory(Event* e) {
    const uint32_t n = align_up(e->size, kEventAlignment);
    return reinterpret_cast<Event*>(reinterpret_cast<uint8_t*>(e) + n);
}

} // namespace insys::low
```

> 注意：`time` 使用“输入时间轴”（单调递增的秒）。后端的 `Now()` 与事件时间必须来自同一时间源。

### 6.2 事件类型

| 事件 | FourCC | 负载 |
|---|---|---|
| `StateEvent` | `STAT` | `FourCC stateFormat` + 全量状态 |
| `DeltaStateEvent` | `DLTA` | `FourCC stateFormat` + `uint32 stateOffset` + 增量 |
| `TextEvent` | `TEXT` | `int32 character`（UTF-32） |
| `IMECompositionEvent` | `IMEC` | 组合字符串 + 光标/选区 |
| `DeviceRemoveEvent` | `DREM` | 无 |
| `DeviceConfigurationEvent` | `DCFG` | 无 |
| `DeviceResetEvent` | `DRST` | `bool hardReset` |

```cpp
struct StateEvent {
    Event header;
    FourCC stateFormat;
    uint8_t stateData[1];   // 变长
};
struct DeltaStateEvent {
    Event header;
    FourCC stateFormat;
    uint32_t stateOffset;   // 相对设备状态起始
    uint8_t deltaState[1];  // 变长
};
struct TextEvent {
    Event header;
    int32_t character;
};
struct DeviceRemoveEvent { Event header; };
struct DeviceConfigurationEvent { Event header; };
struct DeviceResetEvent { Event header; bool hardReset; uint8_t _pad[3]; };
```

事件构造：

```cpp
// 全量状态事件：从设备的当前状态（或默认状态）生成
std::vector<uint8_t> make_state_event(const Device& device, bool useDefaultState = false);

// 增量状态事件：从某个控件的当前状态生成
std::vector<uint8_t> make_delta_state_event(const Control& control);

// 文本事件
std::vector<uint8_t> make_text_event(int deviceId, char32_t ch, double time);
```

### 6.3 事件写入规则

参考：`InputSystem/Runtime/Events/StateEvent.cs`、`DeltaStateEvent.cs`。

- `StateEvent` 的 `stateData` 长度 = `device.stateBlock.sizeInBits / 8`。
- `DeltaStateEvent.stateOffset` 必须是相对设备状态块起始的字节偏移。
- 事件总大小向上对齐到 4/8 字节。
- 事件 `time` 使用 `runtime.Now()`。

### 6.4 事件队列（后端 → 主线程）

后端可能在不同回调/线程中产生事件，主线程在 `Manager::Update` 中消费。使用 MPSC 环形队列：

```cpp
class EventQueue {
public:
    explicit EventQueue(size_t capacityBytes = 1 << 20);
    // 线程安全：复制事件到内部缓冲
    void push(const Event* event);
    // 主线程：交换出待处理缓冲
    EventBuffer drain();
private:
    std::mutex m_mutex;
    std::vector<uint8_t> m_buffer;
};
```

- 写入时按 `align_up(size, kEventAlignment)` 追加。
- 读取时整块 swap 到主线程，避免持锁处理。
- 容量不足时丢弃最旧事件并记录诊断（或按设置丢弃新事件）。

### 6.5 事件缓冲与流

参考：`InputSystem/Runtime/Events/InputEventBuffer.cs`、`InputEventStream.cs`。

```cpp
class EventBuffer {
public:
    int event_count() const;
    uint32_t size_bytes() const;
    uint32_t capacity_bytes() const;
    Event* begin();
    Event* end();
    void reset();
    Event* allocate(int sizeBytes);           // 追加并返回可写位置
    void append(const Event* e);
    void advance_to_next(Event** read, int& remaining, bool keepInBuffer);
};
```

`EventStream` 负责在一次 Update 中同时遍历“原生缓冲”和“本次 Update 内新追加的事件”：

```cpp
class EventStream {
public:
    EventStream(EventBuffer& nativeBuffer, int maxAppendedEvents);
    bool is_open() const;
    int remaining() const;
    Event* current() const;
    Event* peek() const;
    Event* advance(bool keepInBuffer);
    void write(Event* e);                    // 处理过程中 QueueEvent 走这里
    void close(EventBuffer& out);
    void cleanup_after_exception();
};
```

> 关键点：Update 期间调用 `System::QueueEvent` 不能递归处理，必须写入 append buffer；`EventStream` 先消费原生事件，再消费 append 事件。

---

## 7. 状态层

### 7.1 StateBlock

参考：`InputSystem/Runtime/State/InputStateBlock.cs`。

```cpp
struct StateBlock {
    static constexpr uint32_t InvalidOffset = 0xFFFFFFFFu;
    static constexpr uint32_t AutomaticOffset = 0xFFFFFFFEu;

    FourCC format = {};
    uint32_t byteOffset = InvalidOffset;  // 相对设备状态起点
    uint32_t bitOffset = 0;               // 相对 byteOffset
    uint32_t sizeInBits = 0;

    uint32_t aligned_size_in_bytes() const { return bits_to_bytes(sizeInBits); }
    uint32_t effective_byte_offset() const { return byteOffset + (bitOffset >> 3); }
};

// 读取/写入：按 format 解释内存
int32_t  read_int(const StateBlock& b, const void* state);
void     write_int(const StateBlock& b, void* state, int32_t v);
float    read_float(const StateBlock& b, const void* state);
void     write_float(const StateBlock& b, void* state, float v);
bool     read_bit(const StateBlock& b, const void* state);
void     write_bit(const StateBlock& b, void* state, bool v);
```

数值转换规则（与 Unity 保持一致）：

- `BIT`：1 位为 0/1；多位为无符号归一化。
- `SBIT`：1 位为 -1/+1；多位为有符号（excess-K）归一化。
- `INT/UINT/SHRT/USHT/BYTE/SBYT/FLT/DBL`：按类型直接读写；`read_float` 时整数格式归一化到 [-1,1] 或 [0,1]。
- `VEC2/VEC3/QUAT`：作为整体字节块，由具体控件解释。

### 7.2 StateBuffers

参考：`InputSystem/Runtime/State/InputStateBuffers.cs`。

每个设备在所有状态缓冲中占据相同大小的槽位。每种更新类型一套双缓冲：

```cpp
struct DoubleBuffers {
    std::vector<uint8_t> front;
    std::vector<uint8_t> back;
    bool valid = false;
    void swap() { std::swap(front, back); }
};

class StateBuffers {
public:
    void allocate_all(const std::vector<Device*>& devices);
    void free_all();
    void migrate_all(const std::vector<Device*>& devices, const StateBuffers& oldBuffers);

    DoubleBuffers& for_update(UpdateType type);
    uint8_t* front_for(int deviceIndex);
    uint8_t* back_for(int deviceIndex);

    // 全局单缓冲
    std::vector<uint8_t> defaultState;   // 每个设备默认状态
    std::vector<uint8_t> noiseMask;      // 噪声控件掩码
    std::vector<uint8_t> resetMask;      // dontReset 掩码
};
```

布局（单缓冲）：

```
[dev0 state][dev1 state][dev2 state] ...  每个设备按 aligned_size_in_bytes() 向上对齐到 4/8
```

分配策略可与 Unity 一致：一次大分配，内部切成 front/back/default/noise/reset + 设备映射表。

### 7.3 状态变化监视器

参考：`InputSystem/Runtime/InputManagerStateMonitors.cs`、`InputSystem/Runtime/LowLevel/IInputStateChangeMonitor.cs`。

```cpp
struct BitRegion { uint32_t byteOffset; uint32_t bitOffset; uint32_t sizeInBits; };
BitRegion overlap(const BitRegion& a, const BitRegion& b);

struct StateChangeListener {
    Control* control;
    IStateChangeMonitor* monitor;
    int64_t monitorIndex;
    uint32_t groupIndex;
};

struct MonitorsForDevice {
    std::vector<StateChangeListener> listeners;
    std::vector<BitRegion> regions;
    std::vector<bool> signalled;        // 或 DynamicBitfield
    bool needReorder = false;
    bool needCompact = false;
};

class StateChangeMonitors {
public:
    void add(Control* control, IStateChangeMonitor* monitor, int64_t monitorIndex, uint32_t groupIndex);
    void remove(Control* control, IStateChangeMonitor* monitor, int64_t monitorIndex);
    void add_timeout(Control* control, IStateChangeMonitor* monitor, double time,
                     int64_t monitorIndex, int timerIndex);
    void remove_timeout(IStateChangeMonitor* monitor, int64_t monitorIndex, int timerIndex);
    void process_timeouts(double now);

    // 比较事件新状态与设备旧状态在监视区域内的变化，设置 signalled
    bool process_state_change(int deviceIndex, const void* newState, const void* oldState,
                              uint32_t newSize, uint32_t newOffset);

    void fire(int deviceIndex, double time, Event* eventPtr = nullptr);
    void on_device_removed(Device* device);
};
```

算法（与 Unity 一致）：

1. 对每个监听器，计算其 `BitRegion` 与本次事件写入区域的交集。
2. 如果交集为空，跳过。
3. 如果交集内旧状态与新状态按位比较相同，跳过。
4. 否则置 `signalled[i] = true`。
5. `fire` 时按 `monitorIndex` 的复杂度/优先级降序排序；调用 `monitor->notify_state_changed(...)`。
6. 如果回调把事件的 `handled` 置位，则清除同一 `monitor` 且同一 `groupIndex` 的后续信号。
7. 触发后清除对应 bit。

> 这是 Actions 能收到“控件变化”的唯一通道。务必先比较旧状态、再写新状态，避免同一帧多次状态事件互相覆盖导致漏通知。

---

## 8. 控件、布局、设备层

### 8.1 Control 基类

参考：`InputSystem/Runtime/Controls/InputControl.cs`。

```cpp
enum class ControlFlags : uint32_t {
    None = 0,
    Noisy = 1u << 0,
    Synthetic = 1u << 1,
    DontReset = 1u << 2,
    UsesStateFromOtherControl = 1u << 3,
    IsButton = 1u << 4,
    HasDefaultState = 1u << 5,
};

class Control {
public:
    virtual ~Control() = default;

    // 标识
    InternedString name;
    InternedString layout;
    InternedString variants;
    std::string displayName;
    std::string shortDisplayName;
    std::string path;                 // 延迟生成

    // 树
    Device* device = nullptr;
    Control* parent = nullptr;
    uint32_t childStart = 0, childCount = 0;
    uint32_t usageStart = 0, usageCount = 0;
    uint32_t aliasStart = 0, aliasCount = 0;

    // 状态
    StateBlock stateBlock;
    ControlFlags flags = ControlFlags::None;

    // 处理器
    std::vector<std::unique_ptr<IProcessor>> processors;

    // 默认/范围
    std::optional<PrimitiveValue> defaultState;
    std::optional<PrimitiveValue> minValue;
    std::optional<PrimitiveValue> maxValue;

    // 虚接口
    virtual size_t value_size() const = 0;
    virtual ValueType value_type() const = 0;
    virtual void read_value_from_state(const void* state, void* out) const = 0;
    virtual void write_value_to_state(void* state, const void* in) const = 0;
    virtual float evaluate_magnitude(const void* state) const { return 0.0f; }
    virtual bool is_default(const void* state) const = 0;

    // 便捷
    Control* child(const std::string& name) const;
    const std::vector<Control*>& children() const;
};
```

类型化控件：

```cpp
template <typename T>
class ControlT : public Control {
public:
    size_t value_size() const override { return sizeof(T); }
    ValueType value_type() const override { return TypeOf<T>(); }

    T read_value() const {
        // 1) 从当前状态读取原始值
        // 2) 依次应用 processors
        // 3) 缓存（若缓存有效且未失效）
    }
    T read_unprocessed() const;
    void write_value(const T& v);
};
```

具体控件类型（首版）：

| 控件 | 值类型 | 说明 |
|---|---|---|
| `ButtonControl` | `float` | `pressPoint`、`wasPressedThisFrame`、`wasReleasedThisFrame` |
| `AxisControl` | `float` | 标量轴 |
| `KeyControl` | `float` | 键，本质是 Button + 键码 |
| `Vector2Control` | `Vector2` | 二维向量 |
| `Vector3Control` | `Vector3` | 三维向量 |
| `StickControl` | `Vector2` | 摇杆，含 up/down/left/right 合成子控件 |
| `DpadControl` | `Vector2` | 十字键，由 4 个按钮合成 |
| `QuaternionControl` | `Quat` | 朝向 |
| `IntegerControl` / `DoubleControl` | int64/double | 其他数值 |

### 8.2 ControlPath

参考：`InputSystem/Runtime/Controls/InputControlPath.cs`、`InputSystem/Runtime/Utilities/PathParser.cs`。

首版语法（自定义、比 Unity 更简洁）：

```
path        := component ('/' component)*
component   := optional-usage* optional-layout | name
usage       := '{' identifier '}'
layout      := '<' identifier '>'
wildcard    := '*'
```

示例：

```
Keyboard/Space
Mouse/LeftButton
Gamepad/LeftStick/X
<Gamepad>/LeftStick
*/{PrimaryAction}
Gamepad/Dpad/Up
```

匹配规则：

- 大小写不敏感。
- 路径首段可以匹配 layout 或 usage；后续段匹配控件名。
- `*` 匹配任意设备。
- `{Usage}` 匹配带该 usage 的控件。
- `<Layout>` 匹配该 layout 的控件。
- 支持别名（alias）匹配。
- 支持 `PathParser` 返回组件列表，供 rebinding 生成路径。

接口：

```cpp
struct PathComponent {
    InternedString layout;
    InternedString name;
    SmallVector<InternedString, 2> usages;
    bool wildcard = false;
};

std::vector<PathComponent> parse_path(std::string_view path);
bool path_matches(std::string_view path, const Control& control);
Control* find_control(Control& root, std::string_view path);
std::string combine_path(const Control* parent, std::string_view name);
```

### 8.3 Layout 模型

参考：`InputSystem/Runtime/Controls/InputControlLayout.cs`。

```cpp
struct ControlItem {
    InternedString name;
    InternedString layout;
    InternedString variants;
    std::string useStateFrom;
    std::string displayName;
    std::string shortDisplayName;
    SmallVector<InternedString, 2> usages;
    SmallVector<InternedString, 2> aliases;
    SmallVector<NamedValue, 2> parameters;
    SmallVector<NameAndParameters, 2> processors;

    uint32_t offset = StateBlock::AutomaticOffset;
    uint32_t bit = 0;
    uint32_t sizeInBits = 0;
    FourCC format = {};
    int arraySize = 0;

    std::optional<PrimitiveValue> defaultState;
    std::optional<PrimitiveValue> minValue;
    std::optional<PrimitiveValue> maxValue;

    bool isModifyingExistingControl = false;
    bool isNoisy = false;
    bool isSynthetic = false;
    bool dontReset = false;
    bool isFirstDefinedInThisLayout = false;
};

class Layout {
public:
    InternedString name;
    InternedString variants;
    FourCC stateFormat;
    std::string displayName;
    bool? updateBeforeRender;
    bool? canRunInBackground;

    std::vector<ControlItem> controls;
    Layout* baseLayout = nullptr;

    // 工厂：创建控件实例
    std::function<std::unique_ptr<Control>()> factory;

    // 继承合并
    void merge_from(const Layout& base);
};
```

### 8.4 Layout 注册

不依赖 C# 反射。三种注册方式：

1. **代码注册**（推荐）：
```cpp
System::register_layout<Keyboard>("Keyboard", Keyboard::make_layout());
```

2. **JSON 注册**（可选，便于调试）。
3. **预编译布局**（性能选项）。

注册表：

```cpp
class LayoutRegistry {
public:
    void add(std::unique_ptr<Layout> layout);
    Layout* find(std::string_view name);
    void add_matcher(std::string_view layout,
                     const DeviceMatcher& matcher);
    std::string find_matching_layout(const DeviceDescription& desc);
};
```

### 8.5 DeviceBuilder 核心算法

参考：`InputSystem/Runtime/Devices/InputDeviceBuilder.cs`。

这是布局系统最关键的部分。输入：`layoutName`、`variants`、`deviceId`、`description`。输出：完整的 `Device` 控件树，所有 `StateBlock.byteOffset` 已按设备状态缓冲计算好。

步骤：

```
build(layoutName, variants, deviceId, description):
  1. layout = registry.find(layoutName)
  2. device = layout.factory()  // 必须是 Device
     device.name = layout.name
     device.stateBlock.byteOffset = 0
     device.stateBlock.format = layout.stateFormat
  3. instantiate(layout, variants, name, parent=nullptr)
  4. 计算设备状态总大小
  5. finalize：
       - 构建 allControls 数组（深度优先、父子连续）
       - 填充每个控件的 childStart/childCount
       - 填充 usage/alias 数组
       - 构建 stateOffsetToControlMap
  6. 返回 device

instantiate(layout, variants, name, parent):
  control = layout.factory()
  control.layout = layout.name
  control.name = name.empty() ? layout.name : name
  control.parent = parent
  control.device = rootDevice
  for item in layout.controls:
      if item.isModifyingExistingControl:
          add_child_control_if_missing(...)
          continue
      child = instantiate(item.layout, item.variants, item.name, control)
      apply_item(child, item)
      control.add_child(child)
  compute_state_layout(control)
  return control

apply_item(control, item):
  control.flags.noisy = item.isNoisy
  control.flags.synthetic = item.isSynthetic
  control.flags.dontReset = (item.isNoisy || item.dontReset)
  control.defaultState = item.defaultState
  control.minValue/maxValue = item.min/max
  if item.useStateFrom empty:
      control.stateBlock.byteOffset = item.offset
      control.stateBlock.bitOffset = item.bit
      if item.sizeInBits != 0: control.stateBlock.sizeInBits = item.sizeInBits
      if item.format != 0: set_format(control, item)
  else:
      control.stateBlock.sizeInBits = kUseStateFromOtherControl
  add usages/aliases
  apply parameters
  add processors
```

`compute_state_layout(control)` 算法（与 Unity `ComputeStateLayout` 一致）：

```
1. 若 sizeInBits == 0 且 format 是原始格式：
       sizeInBits = primitive_format_bits(format)
2. 若 sizeInBits == 0 且无子控件：报错
3. 若 sizeInBits == 0 且有子控件：由子控件计算
4. 遍历子控件：
     - 跳过 useStateFrom 控件
     - 固定偏移（byteOffset != Invalid/Automatic）：
         计算 endOffset = following_byte_offset(byteOffset, bitOffset + sizeInBits)
         firstUnfixedByteOffset = max(firstUnfixedByteOffset, endOffset)
5. runningByteOffset = firstUnfixedByteOffset
   对每个“自动偏移”子控件：
     - 若 sizeInBits % 8 != 0（位域）：
         若 bitOffset 未定，设为当前 bitfieldSizeInBits
         bitfieldSizeInBits += sizeInBits
     - 否则：
         若之前有未结束的位域，先提交它：runningByteOffset = following_byte_offset(runningByteOffset, bitfieldSizeInBits)
         bitOffset = 0
         runningByteOffset = align_natural(runningByteOffset, aligned_size_in_bytes)
     - 设置 byteOffset = runningByteOffset
     - 非位域：runningByteOffset = following_byte_offset(runningByteOffset, sizeInBits)
6. 若最后还有位域，提交它
7. control.stateBlock.sizeInBits = runningByteOffset * 8
```

关键工具：

```cpp
uint32_t following_byte_offset(uint32_t byteOffset, uint32_t bitOffsetPlusSize) {
    return byteOffset + bits_to_bytes(bitOffsetPlusSize);
}
uint32_t align_natural(uint32_t offset, uint32_t size) {
    // 按 CPU 自然对齐；保守做法：4 或 8 字节
    return align_up(offset, size >= 8 ? 8 : (size >= 4 ? 4 : size));
}
```

> 必须严格实现位域分组和自然对齐，否则 Keyboard 这类 1-bit 控件会出现偏移错误或 ARM 上未对齐访问崩溃。

### 8.6 设备定义（首版）

#### Keyboard

参考：`InputSystem/Runtime/Devices/Keyboard.cs`。

状态为位域，`format = "KEYS"`，`sizeInBits = KeyCount`。每个键占 1 bit：

```cpp
struct KeyboardState {
    static constexpr FourCC format = FourCC('K','E','Y','S');
    // size = (KeyCount + 7) / 8
    uint8_t keys[(KeyCount + 7) / 8];
};
```

布局：

- `anyKey`：合成控件，任意键按下即为 1。
- 每个物理键一个 `KeyControl`，`bit = KeyCode`。
- `shift/ctrl/alt/meta`：`DiscreteButton` 合成，大小为 2 bits（左右各一位），参数 `minValue=1,maxValue=3,writeMode=1`。
- usages：`Back`（Escape）、`Submit`（Enter）、`Modifier`（Shift/Ctrl/Alt/Meta）等。

#### Mouse

参考：`InputSystem/Runtime/Devices/Mouse.cs`。

```cpp
struct MouseState {
    static constexpr FourCC format = FourCC('M','O','U','S');
    Vector2 position;   // offset 0
    Vector2 delta;      // offset 8
    Vector2 scroll;     // offset 16
    uint16_t buttons;   // offset 24
    uint16_t displayIndex; // offset 26
    uint16_t clickCount;   // offset 28
    // 总大小 30
};
```

按钮位：

| 位 | 按钮 |
|---|---|
| 0 | Left |
| 1 | Right |
| 2 | Middle |
| 3 | Forward |
| 4 | Back |

布局还包含 `pressure`、`radius`、`pointerId` 等 Pointer 公共控件，使用自动偏移放在末尾。

#### Gamepad

参考：`InputSystem/Runtime/Devices/Gamepad.cs`。

```cpp
struct GamepadState {
    static constexpr FourCC format = FourCC('G','P','A','D');
    uint32_t buttons;     // offset 0
    Vector2 leftStick;    // offset 4
    Vector2 rightStick;   // offset 12
    float leftTrigger;    // offset 20
    float rightTrigger;   // offset 24
};
```

按钮位（`GamepadButton`）：

```
DpadUp=0, DpadDown=1, DpadLeft=2, DpadRight=3,
North=4, East=5, South=6, West=7,
LeftStick=8, RightStick=9, LeftShoulder=10, RightShoulder=11,
Start=12, Select=13
```

控件命名：

- `buttonSouth/North/West/East`（别名 a/b/x/y、cross/circle/square/triangle）
- `leftStick/rightStick`（`StickControl`，带 `stickDeadzone` 处理器）
- `leftTrigger/rightTrigger`（`ButtonControl`，`format=FLT`）
- `dpad`（`DpadControl`，`format=BIT`，4 bits）
- usages：`PrimaryAction`、`Submit`（South）；`Back`、`Cancel`（East）；`Menu`（Start）等。

#### Joystick

参考：`InputSystem/Runtime/Devices/Joystick.cs`。

```cpp
struct JoystickState {
    static constexpr FourCC format = FourCC('J','O','Y','S');
    uint32_t buttons;
    Vector2 stick;
    float trigger;
    // hatswitch（可选）
};
```

#### Pointer 抽象

参考：`InputSystem/Runtime/Devices/Pointer.cs`。

`Pointer` 是所有指针类设备的基类，提供：
- `position`（屏幕坐标）
- `delta`
- `press`
- `pressure`
- `radius`
- `pointerId`
- usages：`Point`、`PrimaryAction`、`SecondaryAction`、`Pressure`、`Radius`

Mouse/Touchscreen/Pen 均继承 Pointer。

### 8.7 设备描述与匹配

参考：`InputSystem/Runtime/Devices/InputDeviceDescription.cs`、`InputDeviceMatcher.cs`。

```cpp
struct DeviceDescription {
    std::string interfaceName;   // 如 "XInput"、"GLFW"
    std::string deviceClass;     // 如 "Gamepad"
    std::string manufacturer;
    std::string product;
    std::string serial;
    std::string version;
    std::string capabilities;    // JSON 字符串
};

class DeviceMatcher {
public:
    // 字段匹配：精确 / 正则 / 模式
    void add(std::string_view key, std::string_view pattern, bool regex = false);
    float match_score(const DeviceDescription& desc) const; // 0..1
};
```

`Manager::on_find_layout_for_device` 回调允许插件按描述返回布局名。

---

## 9. 核心 Manager

参考：`InputSystem/Runtime/InputManager.cs`。

### 9.1 职责

- 设备注册表（已添加设备、可用设备、断开设备）。
- 事件队列消费与分发。
- 布局/处理器/交互/合成注册表。
- 状态缓冲分配与迁移。
- 状态变化监视器。
- 更新调度（Dynamic/Fixed/BeforeRender）。
- 设备命令（IOCTL）。
- 焦点/重置/禁用。

### 9.2 数据结构

```cpp
class Manager {
public:
    Manager(IRuntime& runtime, const Settings& settings);
    ~Manager();

    // 设备
    const std::vector<Device*>& devices() const;
    Device* add_device(std::string_view layout, std::string name = {});
    void add_device(std::unique_ptr<Device> device);
    void remove_device(Device* device);
    Device* device_by_id(int id);
    Device* device(std::string_view nameOrLayout);

    // 布局
    void register_layout(std::unique_ptr<Layout> layout);
    void register_layout_matcher(...);

    // 事件
    void queue_event(const Event* e);
    void update(UpdateType type);
    void update();  // 使用 settings.updateMask 推导

    // 状态
    bool update_state(Device* device, const Event* stateEvent, UpdateType type);
    bool update_state(Device* device, UpdateType type, const void* state,
                      uint32_t offset, uint32_t size, double time, const Event* e = nullptr);

    // 监视器
    void add_state_change_monitor(Control* control, IStateChangeMonitor* monitor,
                                  int64_t monitorIndex, uint32_t groupIndex);
    void remove_state_change_monitor(Control* control, IStateChangeMonitor* monitor,
                                     int64_t monitorIndex);
    void add_state_change_monitor_timeout(...);
    void remove_state_change_monitor_timeout(...);

    // 命令
    long device_command(int deviceId, DeviceCommand* cmd);

private:
    IRuntime* m_runtime;
    Settings m_settings;

    LayoutRegistry m_layouts;
    TypeTable m_processors;
    TypeTable m_interactions;
    TypeTable m_composites;

    std::vector<std::unique_ptr<Device>> m_devicesOwned;
    std::vector<Device*> m_devices;
    std::unordered_map<int, Device*> m_devicesById;

    std::vector<AvailableDevice> m_availableDevices;
    std::vector<Device*> m_disconnectedDevices;

    UpdateMask m_updateMask;
    UpdateType m_currentUpdate;
    StateBuffers m_stateBuffers;
    StateChangeMonitors m_stateMonitors;

    EventQueue m_eventQueue;
    EventStream m_eventStream;
};
```

### 9.3 添加设备

参考：`InputManager.cs` 中 `AddDevice`。

```
add_device(description, layoutName):
  1. 若 layoutName 为空：layoutName = find_matching_layout(description)
  2. 若仍为空：设备进入 unsupported 列表，返回 null
  3. builder.build(layoutName, variants, deviceId, description) → device
  4. device.deviceId = runtime.allocate_device_id()
  5. device.m_DeviceIndex = append(m_devices)
     m_devicesById[deviceId] = device
  6. 迁移/分配状态缓冲：
       若设备数量变化 → 重新 allocate_all 并 migrate_all
  7. device.notify_added(); 触发 on_device_change(Added)
  8. 若设备有 beforeRender 回调 → 增加 BeforeRender 更新掩码
```

### 9.4 更新主流程

参考：`InputManager.cs:3250`（`OnUpdate`）、`:3345`（`ProcessEventBuffer`）。

```
update(type):
  1. 若 type 未在 updateMask 中：返回
  2. 切换当前状态缓冲：stateBuffers.switch_to(type)
  3. m_currentUpdate = type
  4. currentTime = type == Fixed ? runtime.current_time_for_fixed() : runtime.now()
  5. eventBuffer = eventQueue.drain()
  6. 若 eventBuffer 为空：
       处理超时 → 调用 after_update 回调 → 返回
  7. process_event_buffer(type, eventBuffer)
  8. 处理交互超时：stateMonitors.process_timeouts(currentTime)
  9. finalize_update(type)
```

`process_event_buffer`：

```
stream = EventStream(eventBuffer, settings.maxQueuedEventsPerUpdate)
while stream.remaining() > 0:
    device = try_get_device_by_id(event.deviceId)
    if device == null && event.type != focus: skip
    if device disabled && event 不是 Remove/Configuration: skip
    if event 时间 >= currentTime 且 timeslice: 保留到下次
    if 设备可合并事件: try_merge_with_next_event
    if 设备有 event preprocessor: if !device.preprocess(event): skip
    if event listeners 非空: 调用 onEvent；若 handled 并按策略: skip
    process_event(device, type, event)
    stream.advance(keepInBuffer=false)
```

`process_event` 分派：

| 事件 | 处理 |
|---|---|
| `StateEvent` / `DeltaStateEvent` | `process_state_event` |
| `TextEvent` | 转给 `ITextInputReceiver` |
| `IMECompositionEvent` | 转给 `ITextInputReceiver` |
| `DeviceRemoveEvent` | `remove_device` + 事件 |
| `DeviceConfigurationEvent` | 通知配置变化 |
| `DeviceResetEvent` | `reset_device` |

`process_state_event`：

```
1. 若 event.time < device.lastUpdateTime 且设备不是 StateCallbackReceiver：
       丢弃（Android 键盘等特例可按需保留）
2. 若设备实现 IInputStateCallbackReceiver：
       调用 device.on_state_event(event)
   否则：
       若 event.format != device.stateBlock.format：丢弃
       update_state(device, event, type)
3. 更新 device.lastUpdateTime = event.time
4. 若状态变化（非噪声）→ device.make_current()
```

### 9.5 UpdateState（最关键）

参考：`InputManager.cs:4256`（`UpdateState`）。

```
update_state(device, type, statePtr, offsetInDevice, size, time, event):
  1. deviceBuffer = stateBuffers.front_for(deviceIndex)
  2. 对监视器排序（若无信号）
  3. haveSignalled = stateMonitors.process_state_change(
         deviceIndex,
         statePtr,                         // 新状态
         deviceBuffer + device.stateBlock.byteOffset,  // 旧状态
         size, offsetInDevice)
  4. deviceStatePtr = deviceBuffer + device.stateBlock.byteOffset + offsetInDevice
  5. noiseMask = device.noisy ? noiseMaskBuffer + ... : nullptr
  6. makeCurrent = !memcmp_bit_region(deviceStatePtr, statePtr, 0, size*8, noiseMask)
  7. 若需要翻页：flip_buffers_for_device_if_necessary(device, type)
  8. write_state_change(...)：
       - 若本次只更新部分状态，先从前台缓冲补齐其余部分（仅在翻页后第一次）
       - 复制新状态到前台缓冲
  9. 若 makeCurrent：
       更新所有按钮的 wasPressedThisFrame/wasReleasedThisFrame
 10. 通知 on_device_state_change
 11. 若 haveSignalled：stateMonitors.fire(deviceIndex, time, event)
```

`flip_buffers_for_device_if_necessary`：

- 若 `device.lastUpdateStepCount != currentUpdateStepCount`：
  - `buffers.swap()`
  - `device.lastUpdateStepCount = currentUpdateStepCount`
  - 返回 true
- 否则返回 false。

> 这一步保证每个设备在每次 Update 中只翻页一次，从而 `wasPressedThisFrame` 等语义正确。

### 9.6 焦点、重置、禁用

- 焦点状态：`runtime.focus_state`（ApplicationFocus / Editor / MousePresent）。
- 失焦且不允许后台运行：禁设备，清空状态，恢复时同步。
- `reset_device`：把设备状态恢复到默认状态；`dontReset` 控件除外。
- `enable_or_disable_device`：设置 `DisabledInFrontend` 标志，并通知 Actions。

---

## 10. 运行时抽象与 GLFW 后端

### 10.1 IRuntime

参考：`InputSystem/Runtime/IInputRuntime.cs`、`NativeInputRuntime.cs`。

```cpp
namespace insys {

struct FocusState {
    bool applicationFocus = true;
    bool editorActive = true;
    bool mousePresent = true;
};

class IRuntime {
public:
    virtual ~IRuntime() = default;

    virtual int allocate_device_id() = 0;
    virtual void queue_event(const low::Event* event) = 0;
    virtual long device_command(int deviceId, DeviceCommand* cmd) = 0;

    virtual double now() const = 0;                       // 输入时间轴
    virtual double now_for_fixed() const = 0;
    virtual float  unscaled_game_time() const = 0;

    virtual Vector2 screen_size() const = 0;
    virtual int screen_orientation() const = 0;

    virtual bool run_in_background() const = 0;
    virtual void set_run_in_background(bool v) = 0;

    virtual FocusState focus_state() const = 0;
    virtual void set_focus_state(FocusState s) = 0;

    virtual float polling_frequency() const = 0;
    virtual void set_polling_frequency(float hz) = 0;

    // 回调（由 Manager 安装）
    std::function<void(UpdateType, EventBuffer&)> on_update;
    std::function<void(UpdateType)> on_before_update;
    std::function<bool(UpdateType)> on_should_run_update;
    std::function<void(int deviceId, DeviceDescription)> on_device_discovered;
    std::function<void(bool)> on_focus_changed;
    std::function<void()> on_shutdown;
};

} // namespace insys
```

### 10.2 IPlatformBackend

```cpp
namespace insys::backend {

class IPlatformBackend {
public:
    virtual ~IPlatformBackend() = default;

    // 生命周期
    virtual bool initialize(IWindow& window) = 0;
    virtual void shutdown() = 0;

    // 每帧调用：把平台输入转换为事件并入队
    virtual void poll_events(IRuntime& runtime) = 0;

    // 设备发现
    virtual void enumerate_devices(
        std::function<void(const DeviceDescription&)> onDevice) = 0;
};

class IWindow {
public:
    virtual ~IWindow() = default;
    virtual Vector2 size() const = 0;
    virtual Vector2 framebuffer_size() const = 0;
    virtual Vector2 content_scale() const = 0;
    virtual bool focused() const = 0;
    virtual void* native_handle() const = 0;
};

class ITime {
public:
    virtual ~ITime() = default;
    virtual double now() const = 0;
    virtual double fixed_time() const = 0;
    virtual float  unscaled_time() const = 0;
    virtual float  delta_time() const = 0;
};

class IScheduler {
public:
    virtual ~IScheduler() = default;
    // 由宿主在主循环调用
    virtual void begin_frame() = 0;
    virtual bool next_fixed_step() = 0;   // 返回是否还需一次 Fixed Update
    virtual void end_frame() = 0;
};

} // namespace insys::backend
```

### 10.3 GLFW 后端

参考 Unity 端 `Runtime/Plugins` 中平台后端思路，但输入源换成 GLFW。

#### 回调安装（唯一安装者）

```cpp
class GlfwBackend final : public IPlatformBackend {
public:
    bool initialize(IWindow& window) override;
    void shutdown() override;
    void poll_events(IRuntime& runtime) override;
    void enumerate_devices(DeviceDiscoveredFn onDevice) override;

private:
    static void on_key(GLFWwindow*, int key, int scancode, int action, int mods);
    static void on_char(GLFWwindow*, unsigned int codepoint);
    static void on_cursor_pos(GLFWwindow*, double x, double y);
    static void on_mouse_button(GLFWwindow*, int button, int action, int mods);
    static void on_scroll(GLFWwindow*, double x, double y);
    static void on_joystick(int jid, int event);

    void emit_keyboard(int key, int action, int mods);
    void emit_mouse();
    void emit_text(unsigned int codepoint);
    void poll_gamepads();

    GLFWwindow* m_window = nullptr;
    MouseState m_mouse{};          // 维护当前鼠标状态
    KeyboardState m_keyboard{};
    std::unordered_map<int, GamepadState> m_gamepads;
};
```

#### 键盘映射

- `GLFW_KEY_*` → 内部 `KeyCode` → 设置 `KeyboardState` 对应 bit。
- `GLFW_PRESS/RELEASE/REPEAT`：
  - PRESS：置位，产生 StateEvent。
  - RELEASE：清位，产生 StateEvent。
  - REPEAT：产生 TextEvent 或按键重复事件（可选）。
- `GLFW_MOD_SHIFT/CONTROL/ALT/SUPER` 同步到 Shift/Ctrl/Alt/Meta 的左右位（GLFW 不区分左右时默认左）。

#### 鼠标映射

- 光标位置：`glfwGetCursorPos` → `MouseState.position`。
- 位移：`position - lastPosition` → `MouseState.delta`（或使用 Raw Input 获取原始 delta）。
- 按键：`GLFW_MOUSE_BUTTON_LEFT/RIGHT/MIDDLE` → `buttons` 位。
- 滚轮：`glfwSetScrollCallback` → `scroll`。
- 每次状态变化产生 `StateEvent`（或 `DeltaStateEvent`）。

#### 手柄映射

- `glfwJoystickPresent` / `glfwJoystickIsGamepad`。
- `glfwGetGamepadState(jid, &state)` 读取：
  - `GLFW_GAMEPAD_BUTTON_A/B/X/Y`、`LEFT_BUMPER/RIGHT_BUMPER`、`BACK/START`、`GUIDE`、`LEFT_THUMB/RIGHT_THUMB`、`DPAD_*`。
  - `GLFW_GAMEPAD_AXIS_LEFT_X/Y`、`RIGHT_X/Y`、`LEFT_TRIGGER/RIGHT_TRIGGER`。
- 映射到 `GamepadState`：
  - South=A、East=B、West=X、North=Y。
  - Dpad 位由 `GLFW_GAMEPAD_BUTTON_DPAD_*` 设置。
  - 摇杆直接复制。
  - 扳机：GLFW 返回 [-1,1]，转换为 [0,1]。
- `glfwSetJoystickCallback` 处理热插拔：连接 → 枚举并 `on_device_discovered`；断开 → 产生 `DeviceRemoveEvent`。

#### 轮询与 diff

```cpp
void GlfwBackend::poll_gamepads() {
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickIsGamepad(jid)) continue;
        GLFWgamepadstate glfwState;
        if (!glfwGetGamepadState(jid, &glfwState)) continue;

        GamepadState newState = map_gamepad(glfwState);
        auto& oldState = m_gamepads[jid];
        if (memcmp(&oldState, &newState, sizeof(newState)) != 0) {
            emit_state_event(gamepad_device_id(jid), &newState, sizeof(newState));
            oldState = newState;
        }
    }
}
```

> 推荐：手柄用“轮询 + diff”，键鼠用“回调”。这样可以避免手柄状态事件洪泛。

### 10.4 宿主循环约定

```cpp
insys::System sys;                       // 内部创建 Manager + GlfwBackend
sys.initialize(window);

while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();                    // GLFW 回调 → 后端事件队列
    sys.update(UpdateType::Dynamic);     // 处理动态事件
    while (fixedAccumulator >= fixedDt) {
        sys.update(UpdateType::Fixed);
        fixedAccumulator -= fixedDt;
    }
    uiAdapter.flush(rmlContext);         // 输入 → RmlUi
    rmlContext.Update();
    render();                            // 宿主/Demo 渲染
    fixedAccumulator += dt;
}
```

---

## 11. Actions 层

### 11.1 数据模型

参考：`InputSystem/Runtime/Actions/InputAction.cs`、`InputActionMap.cs`、`InputActionAsset.cs`、`InputBinding.cs`。

```cpp
enum class ActionType { Value, Button, PassThrough };

enum class ActionPhase {
    Disabled, Waiting, Started, Performed, Canceled
};

struct Binding {
    std::string name;
    Guid id;
    std::string path;
    std::string overridePath;        // 运行时覆盖
    std::string action;              // action 名或 id
    std::string interactions;        // "tap;hold"
    std::string processors;          // "stickDeadzone(min=0.2,max=0.9)"
    std::string groups;              // "Gamepad;Keyboard&Mouse"
    std::string overrideInteractions;
    std::string overrideProcessors;
    bool isComposite = false;
    bool isPartOfComposite = false;
    int partIndex = -1;              // 由 resolver 填充
};

struct ActionDef {
    std::string name;
    Guid id;
    ActionType type = ActionType::Button;
    std::string expectedControlType;
    std::string processors;
    std::string interactions;
    bool initialStateCheck = false;
    std::vector<Binding> bindings;
};

class ActionMap {
public:
    std::string name;
    Guid id;
    std::vector<ActionDef> actions;
    std::vector<Binding> bindings;

    void enable();
    void disable();
    bool enabled() const;
    ActionDef* find_action(std::string_view nameOrId, bool throwIfNotFound = false);
    ActionDef* operator[](std::string_view name);
};

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

### 11.2 绑定解析（BindingResolver）

参考：`InputSystem/Runtime/Actions/InputBindingResolver.cs`。

输入：一组 `ActionMap`（共享一个 `ActionState`）。输出：填充好的 `UnmanagedMemory` + 实例化的处理器/交互/合成器。

算法：

```
resolve(maps):
  1. 统计：
       mapCount, actionCount, bindingCount
       compositeCount = isComposite 的 binding 数
       controlCount = 所有普通 binding 解析到的控件总数
       interactionCount = 所有 binding 的 interaction 总数
       processorCount = 所有 binding 的 processor 总数
  2. allocate UnmanagedMemory
  3. mapIndex = 0
     bindingIndex = 0
     controlIndex = 0
     interactionIndex = 0
     processorIndex = 0
     compositeIndex = 0
  4. 对每个 map：
       mapIndices[mapIndex] = { actionStart, actionCount, bindingStart, bindingCount, controlStart, controlCount }
       for each action:
         actionStates[actionIndex] = { phase=Disabled, flags, ... }
         actionBindingIndicesAndCounts[actionIndex] = { bindingStart, bindingCount }
       for each binding:
         bindingStates[bindingIndex] = 基础信息
         if binding.isComposite:
             composite = 实例化 composite(path 中的名字)
             composite->partIndex = ...
             bindingStates[bindingIndex].flags |= Composite
             bindingStates[bindingIndex].compositeOrCompositeBindingIndex = compositeIndex
         else if binding.isPartOfComposite:
             bindingStates[bindingIndex].flags |= PartOfComposite
             bindingStates[bindingIndex].compositeOrCompositeBindingIndex = 所属 composite 的 bindingIndex
             bindingStates[bindingIndex].partIndex = composite->assign_part(binding.name)
             resolve path → controls
         else:
             resolve path → controls
         for each control:
             controls[controlIndex] = control
             controlIndexToBindingIndex[controlIndex] = bindingIndex
             bindingStates[bindingIndex].controlStartIndex = ...
             bindingStates[bindingIndex].controlCount = ...
             controlIndex++
         instantiate processors → processors[processorIndex...]
         instantiate interactions → interactions[interactionIndex...]
  5. 计算 controlGroupingAndPriority（可选，用于快捷路径冲突）
  6. 返回
```

路径解析：

- `binding.effective_path()` = `overridePath` 非空 ? `overridePath` : `path`。
- 使用 `find_controls(deviceRoot, path)` 返回 0..N 个控件。
- 若绑定使用 `<Gamepad>` 或 `*`，会匹配多个设备。
- 普通 binding 可能匹配多个控件；每个控件都参与该 binding。

### 11.3 ActionState 内存布局

参考：`InputSystem/Runtime/Actions/InputActionState.cs`（`UnmanagedMemory`）。

单个 arena，按以下顺序切分（double 字段优先保证 8 字节对齐）：

```
[ActionMapIndices * mapCount]
[TriggerState     * actionCount]
[InteractionState * interactionCount]
[BindingState     * bindingCount]
[float controlMagnitudes * controlCount]
[float compositeMagnitudes * compositeCount]
[int controlIndexToBindingIndex * controlCount]
[ushort controlGroupingAndPriority * controlCount * 2]
[ushort actionBindingIndicesAndCounts * actionCount * 2]
[ushort actionBindingIndices * bindingCount]
[uint32 enabledControlsBits * (controlCount+31)/32]
```

结构体（与 Unity 对齐语义）：

```cpp
struct TriggerState {
    uint8_t  phase;          // ActionPhase
    uint8_t  flags;          // HaveMagnitude / PassThrough / Button
    uint8_t  mapIndex;
    uint16_t controlIndex;   // 0xFFFF = invalid
    double   time;
    double   startTime;
    uint16_t bindingIndex;
    uint16_t interactionIndex;
    float    magnitude;
    uint32_t lastPerformedInUpdate;
    uint32_t lastCanceledInUpdate;
    uint32_t pressedInUpdate;
    uint32_t releasedInUpdate;
    uint32_t lastCompletedInUpdate;
    int32_t  framePerformed;
    int32_t  framePressed;
    int32_t  frameReleased;
    int32_t  frameCompleted;
};

struct BindingState {
    uint8_t  controlCount;
    uint8_t  interactionCount;
    uint8_t  processorCount;
    uint8_t  mapIndex;
    uint8_t  flags;          // ChainsWithNext/EndOfChain/Composite/PartOfComposite/...
    uint8_t  partIndex;
    uint16_t actionIndex;
    uint16_t compositeOrCompositeBindingIndex;
    uint16_t processorStartIndex;
    uint16_t interactionStartIndex;
    uint16_t controlStartIndex;
    double   pressTime;
    int32_t  triggerEventIdForComposite;
};

struct InteractionState {
    uint16_t triggerControlIndex;
    uint8_t  phase;
    uint8_t  flags;
    float    timerDuration;
    double   startTime;
    double   timerStartTime;
    double   performedTime;
    float    totalTimeoutCompletionDone;
    float    totalTimeoutCompletionTimeRemaining;
    int64_t  timerMonitorIndex;
};
```

### 11.4 运行时流程：控件变化 → 动作相位

参考：`InputActionState.cs:1566`（`ProcessControlStateChange`）、`:2103`（`ProcessDefaultInteraction`）、`:2367`（`ChangePhaseOfInteraction`）。

`ActionState` 实现 `IStateChangeMonitor`。当某个被绑定控件变化时：

```
on_control_state_changed(control, time, event, monitorIndex):
  mapIndex/controlIndex/bindingIndex 从 monitorIndex 解码
  m_inProcess = true
  trigger = {
     mapIndex, controlIndex, bindingIndex,
     interactionIndex = invalid,
     time = startTime = time,
     isPassThrough = action.isPassThrough,
     isButton = action.isButton
  }
  trigger.magnitude = control.is_default() ? 0 : control.magnitude
  controlMagnitudes[controlIndex] = trigger.magnitude

  if control.IsValueConsideredPressed(magnitude):
     binding.pressTime = min(binding.pressTime, time) 或 time

  if binding.isPartOfComposite:
     if composite 已被同一 event 触发: return
     trigger.magnitude = composite.evaluate_magnitude(context)
     compositeMagnitudes[compositeIndex] = trigger.magnitude
     if composite 有 interactions: process_interactions(...)

  isConflicting = is_conflicting_input(trigger, actionIndex)   // 可能切换 binding
  if !isConflicting: process_button_state(...)
  if binding 有 interactions 且不是 composite part:
     process_interactions(trigger, ...)
  else if 没有 composite interactions 且 !isConflicting:
     process_default_interaction(trigger, actionIndex)
```

`ProcessDefaultInteraction` 状态机（与 Unity 完全一致）：

| 当前相位 | Action 类型 | 条件 | 迁移 |
|---|---|---|---|
| Waiting | PassThrough | 任意值变化 | `Performed` → 回 `Waiting` |
| Waiting | Button | magnitude > 0 | `Started` |
| Waiting | Button | magnitude >= pressPoint | `Performed`（保持在 Performed） |
| Waiting | Value | IsActuated | `Started` → `Performed` → 回 `Started` |
| Started | Button | magnitude >= pressPoint | `Performed` |
| Started | Button | magnitude ≈ 0 | `Canceled` |
| Started | Value | !IsActuated | `Canceled` |
| Started | Value | IsActuated | `Performed`（保持 Started） |
| Performed | Button | magnitude ≈ 0 | `Canceled` |
| Performed | Button | magnitude <= pressPoint * releaseThreshold | `Started` |
| Performed | PassThrough | 任意值变化 | `Performed` |

相位变化：

```cpp
void change_phase_of_action(ActionPhase newPhase, TriggerState& trigger,
                            ActionPhase phaseAfterPerformedOrCanceled = Invalid);
```

- 更新 `TriggerState`（time/startTime、lastPerformed/lastCanceled、frame*）。
- 调用 `Action::started/performed/canceled` 回调。
- 回调期间禁止重新解析绑定（用 defer 机制）。

### 11.5 冲突消解

参考：`InputActionState.IsConflictingInput`。

- 仅 `Button` / `Value` 做冲突消解；`PassThrough` 不做。
- 同一 action 绑定多个控件时，按 magnitude 选择“最强”控件。
- 若当前驱动控件仍然最大，忽略其他输入。
- 若出现更大 magnitude 的其他控件，切换 binding/control。
- 记录每个 control 的 magnitude 用于比较。

### 11.6 Interaction

接口（参考 `IInputInteraction`）：

```cpp
class IInteraction {
public:
    virtual ~IInteraction() = default;
    virtual void process(InteractionContext& context) = 0;
    virtual void reset() = 0;
};
```

`InteractionContext` 提供：

```cpp
class InteractionContext {
public:
    Action* action();
    Control* control();
    ActionPhase phase() const;
    double time() const;
    double startTime() const;
    bool timerHasExpired() const;
    float compute_magnitude();
    bool control_is_actuated(float threshold = 0);

    void started();
    void performed();
    void performed_and_stay_started();
    void performed_and_stay_performed();
    void canceled();
    void waiting();
    void set_timeout(float seconds);
    void set_total_timeout_completion_time(float seconds);
};
```

首版实现：

| Interaction | 语义 |
|---|---|
| `Tap` | 按下后 `duration` 内释放 → Performed；超时 → Canceled |
| `Hold` | 按下 Started，持续 `duration` → Performed 并保持；释放 → Canceled |
| `SlowTap` | 按下 Started，`duration` 后释放 → Performed；提前释放 → Canceled |
| `Press` | 按下/释放分别触发，可配置行为 |
| `MultiTap` | 连续 `tapCount` 次 Tap，间隔 ≤ `tapDelay` |

Tap 实现要点（参考 `TapInteraction.cs`）：

```
if timerHasExpired: canceled(); canceledFromTimer = true; return
if isWaiting && control_is_actuated(pressPoint) && !canceledFromTimer:
    startTime = time; started(); set_timeout(duration + 0.00001); return
if isStarted && !control_is_actuated(releasePoint):
    if time - startTime <= duration: performed()
    else: canceled()
if !control_is_actuated(releasePoint): canceledFromTimer = false
```

Hold 实现要点（参考 `HoldInteraction.cs`）：

```
if timerHasExpired: performed_and_stay_performed(); return
switch phase:
  Waiting:
     if control_is_actuated(pressPoint): timePressed = time; started(); set_timeout(duration)
  Started:
     if time - timePressed >= duration: performed_and_stay_performed()
     if !control_is_actuated(): canceled()
  Performed:
     if !control_is_actuated(pressPoint): canceled()
```

### 11.7 Composite

接口（参考 `InputBindingComposite`）：

```cpp
class IComposite {
public:
    virtual ~IComposite() = default;
    virtual ValueType value_type() const = 0;
    virtual void read_value(CompositeContext& ctx, void* out) const = 0;
    virtual float evaluate_magnitude(CompositeContext& ctx) const = 0;
    // 由 resolver 调用，把 part 名映射到 part index
    virtual int assign_part(std::string_view name, int& currentPartIndex) = 0;
};
```

`CompositeContext` 提供：

```cpp
template <typename T> T read_value(int partIndex) const;
bool read_value_as_button(int partIndex) const;
```

首版实现：

| Composite | 说明 |
|---|---|
| `Axis` | 正/负两个按钮合成 [-1,1] 轴 |
| `Vector2` | up/down/left/right 合成 Vector2，支持 DigitalNormalized/Digital/Analog |
| `Vector3` | up/down/left/right/forward/backward 合成 Vector3 |
| `ButtonWithOneModifier` | 一个修饰键 + 一个按钮 |
| `ButtonWithTwoModifiers` | 两个修饰键 + 一个按钮 |
| `OneModifier` / `TwoModifiers` | 修饰键组合 |

Vector2 合成（参考 `Vector2Composite.cs`）：

```cpp
Vector2 make_dpad_vector(bool up, bool down, bool left, bool right, bool normalize) {
    float x = (right ? 1 : 0) - (left ? 1 : 0);
    float y = (up ? 1 : 0) - (down ? 1 : 0);
    Vector2 v{x, y};
    if (normalize) v = normalize(v);
    return v;
}
```

### 11.8 Processor

接口（参考 `InputProcessor`）：

```cpp
class IProcessor {
public:
    virtual ~IProcessor() = default;
    virtual ValueType value_type() const = 0;
    virtual void process(void* value, size_t size, const Control* control) const = 0;
    virtual CachingPolicy caching_policy() const { return CachingPolicy::CacheResult; }
};
```

首版实现：

| Processor | 参数 | 说明 |
|---|---|---|
| `Invert` | | `-v` |
| `InvertVector2` | `invertX`, `invertY` | 分轴取反 |
| `InvertVector3` | `invertX/Y/Z` | |
| `Scale` | `factor` | `v * factor` |
| `ScaleVector2` | `x`, `y` | |
| `ScaleVector3` | `x`, `y`, `z` | |
| `Normalize` | `min`, `max`, `zero` | 归一化 |
| `NormalizeVector2/3` | | |
| `Clamp` | `min`, `max` | 截断 |
| `AxisDeadzone` | `min`, `max` | 单轴死区 |
| `StickDeadzone` | `min`, `max` | 摇杆死区（径向） |

### 11.9 启用/禁用与初始状态检查

参考：`InputActionState.EnableAllActions/DisableAllActions/OnBeforeInitialUpdate`。

启用 ActionMap：

```
for action in map.actions:
    actionState.phase = Waiting
for binding in map.bindings:
    for control in binding.controls:
        set_control_enabled(controlIndex, true)
    if binding.wants_initial_state_check:
        binding.initialStateCheckPending = true
    注册 StateChangeMonitor：
        manager.add_state_change_monitor(control, this, packedMonitorIndex, groupIndex)
```

初始状态检查：

- 仅 `Value` 类型且 `initialStateCheck` 或绑定带 `wantsInitialStateCheck` 时执行。
- 在下一个 Update 的 before 阶段，读取控件当前值；若已 actuated，则直接触发 Started/Performed。
- 检查后清除 pending 标志，避免同一状态变化重复触发。

禁用：

- 取消所有进行中的 action（Canceled 回调）。
- 移除状态监视器。
- 清除 timeout。
- 复位 interaction 状态。

### 11.10 重绑定

参考：`InputActionRebindingExtensions.cs`（`RebindingOperation`）。

首版能力：

- 对指定 binding 交互式重绑定。
- 监听 `System::on_event` 或控件状态变化。
- 过滤：排除鼠标位置、噪声控件、指定路径、超时。
- 评分：`magnitude`、按键类型、布局匹配。
- 生成路径：`<Device>/<Control>`（可泛化，例如 `<Keyboard>/anyKey` 不泛化）。
- 应用：写入 `binding.overridePath`，重新解析；支持保存/加载 override JSON。

接口草案：

```cpp
class RebindingOperation {
public:
    RebindingOperation& with_action(ActionDef& action);
    RebindingOperation& with_target_binding(int bindingIndex);
    RebindingOperation& with_expected_control_type(std::string_view layout);
    RebindingOperation& with_controls_excluding(std::string_view path);
    RebindingOperation& with_timeout(float seconds);
    RebindingOperation& on_match(std::function<void(Control*)>);
    RebindingOperation& on_cancel(std::function<void()>);
    RebindingOperation& on_complete(std::function<void()>);

    RebindingOperation& start();
    void cancel();
    void dispose();
};
```

### 11.11 ControlScheme（可选，首版可简化）

参考：`InputSystem/Runtime/Actions/InputControlScheme.cs`、`InputUser.cs`。

首版可只实现：
- `ControlScheme { name, bindingGroup, devices[] }`
- 判断某组设备是否满足 scheme。
- 根据 scheme 过滤 binding groups。

玩家配对（InputUser）可后置。

---

## 12. RmlUi 输入集成

### 12.1 设计原则

1. **RmlUi 只负责渲染/布局/交互，不接管平台输入**。
2. **本库是唯一输入源**：GLFW 回调只由 `GlfwBackend` 安装；不要启用 RmlUi 自带的 GLFW backend 输入处理。
3. RmlUi 的 `RenderInterface` 由宿主/Demo 提供（可用官方 `RmlUi_Renderer_GL3`）。
4. 本库提供 `RmlUiSystemInterface`（时间/光标/剪贴板/日志）和 `RmlUiInputAdapter`（输入翻译）。

> 若同时启用 RmlUi 的 GLFW backend 和本库后端，会出现“一次点击处理两次”的问题。必须在 Demo 中明确只用 Renderer，不用 Backend 输入。

### 12.2 RmlUiInputAdapter 接口

```cpp
namespace insys::ui {

enum class NavigationSource { RawControls, Actions };
enum class CapturePolicy {
    Full,             // UI 打开时吞噬所有输入
    PointerAndText,   // 只吞噬指针与文本
    None              // 不吞噬
};

class RmlUiInputAdapter {
public:
    void set_context(Rml::Context* context);
    void set_navigation_source(NavigationSource source);
    void set_capture_policy(CapturePolicy policy);

    // 每帧在 System::Update 之后调用
    void flush();

    // 查询
    bool pointer_over_ui() const;
    bool is_consuming_keyboard() const;
    bool is_consuming_pointer() const;

    // 可选：UI 动作绑定
    void set_ui_actions(ActionDef* navigate, ActionDef* submit,
                        ActionDef* cancel, ActionDef* point,
                        ActionDef* click, ActionDef* scroll);
};

} // namespace insys::ui
```

### 12.3 输入映射表

#### 鼠标

| 输入 | RmlUi 调用 |
|---|---|
| 位置（像素） | `Context::ProcessMouseMove(x, y, mods)` |
| 左键按下/抬起 | `ProcessMouseButtonDown(0, mods)` / `ProcessMouseButtonUp(0, mods)` |
| 右键 | `1` |
| 中键 | `2` |
| 滚轮 | `ProcessMouseWheel(delta, mods)`（版本相关，见 12.6） |
| 指针离开窗口 | `ProcessMouseLeave()` |

坐标转换：

```
x = mouse.position.x * contentScale
y = mouse.position.y * contentScale
```

或直接使用 framebuffer 坐标，需与 RmlUi 的 `Context::SetDimensions` 保持一致。

#### 键盘

| 输入 | RmlUi 调用 |
|---|---|
| 按下 | `ProcessKeyDown(Rml::Input::KeyIdentifier, mods)` |
| 抬起 | `ProcessKeyUp(Rml::Input::KeyIdentifier, mods)` |
| 字符 | `ProcessTextInput(character)` |
| 文本（UTF-8 字符串） | `ProcessTextInput(string)` |

键映射表（GLFW → RmlUi）：

| GLFW | RmlUi |
|---|---|
| `GLFW_KEY_A..Z` | `KI_A..KI_Z` |
| `GLFW_KEY_0..9` | `KI_0..KI_9` |
| `GLFW_KEY_F1..F12` | `KI_F1..KI_F12` |
| `GLFW_KEY_UP/DOWN/LEFT/RIGHT` | `KI_UP/DOWN/LEFT/RIGHT` |
| `GLFW_KEY_ENTER` | `KI_RETURN` |
| `GLFW_KEY_ESCAPE` | `KI_ESCAPE` |
| `GLFW_KEY_TAB` | `KI_TAB` |
| `GLFW_KEY_BACKSPACE` | `KI_BACK` |
| `GLFW_KEY_DELETE` | `KI_DELETE` |
| `GLFW_KEY_HOME/END/PAGE_UP/PAGE_DOWN` | 对应 |
| `GLFW_KEY_LEFT_SHIFT/RIGHT_SHIFT` | `KI_LSHIFT/KI_RSHIFT` |
| `GLFW_KEY_LEFT_CONTROL/RIGHT_CONTROL` | `KI_LCTRL/KI_RCTRL` |
| `GLFW_KEY_LEFT_ALT/RIGHT_ALT` | `KI_LALT/KI_RALT` |
| `GLFW_KEY_LEFT_SUPER/RIGHT_SUPER` | `KI_LMETA/KI_RMETA` |

修饰键状态：

```cpp
int mods = 0;
if (shift) mods |= Rml::Input::KM_SHIFT;
if (ctrl)  mods |= Rml::Input::KM_CTRL;
if (alt)   mods |= Rml::Input::KM_ALT;
if (super) mods |= Rml::Input::KM_META;
```

#### 文本输入与 IME

- 普通字符：GLFW `CharCallback` → UTF-32 → `ProcessTextInput`。
- 组合字符（如中文 IME）：GLFW 不直接提供 IME 组合串；需要在 Windows 上额外接入 IMM32（`WM_IME_*`）或使用 `RmlUi` 的文本输入接口转发。首版可只支持 ASCII/直接输入，IME 作为 M4 独立项。
- 文本输入框聚焦时，键盘按键仍要转发 `ProcessKeyDown/Up`，以便 Backspace/Enter/方向键工作。

#### 导航（手柄）

两种模式：

1. **RawControls**：直接读 `Gamepad/dpad`、`Gamepad/buttonSouth`、`Gamepad/buttonEast`，转换为方向键/回车/ESC。
2. **Actions**：读 `UI/Navigate`、`UI/Submit`、`UI/Cancel`，转换为方向键/回车/ESC。推荐用于可改键场景。

```cpp
void RmlUiInputAdapter::flush_navigation_actions() {
    Vector2 nav = uiNavigate ? uiNavigate->read_value<Vector2>() : Vector2{};
    if (nav.x < -deadzone) process_key_down(KI_LEFT, mods);
    if (nav.x >  deadzone) process_key_down(KI_RIGHT, mods);
    // ... 抬起时 ProcessKeyUp
    if (uiSubmit->was_pressed_this_frame()) process_key_down(KI_RETURN, mods);
    if (uiCancel->was_pressed_this_frame()) process_key_down(KI_ESCAPE, mods);
}
```

### 12.4 焦点与消费

- `ProcessMouseButtonDown/Up` 返回 `bool` 表示是否被 UI 消费。
- 适配器每帧记录：
  - `m_pointerConsumed`
  - `m_keyboardConsumed`
- 若 `CapturePolicy::Full` 且 UI 打开：当帧游戏 Action 不触发。
- 若 `CapturePolicy::PointerAndText`：只吞噬指针与文本，移动/战斗等仍可触发。
- 提供 `IsConsumingKeyboard/Pointer` 给游戏层做门控。

> 与 Unity `InputSystemUIInputModule` 的差异：Unity 用 `EventSystem` 射线检测 GameObject；RmlUi 由 `Context` 自己维护 hover/focus，适配器只需转发事件并读取返回值。

### 12.5 SystemInterface

```cpp
class RmlUiSystemInterface : public Rml::SystemInterface {
public:
    double GetElapsedTime() override;                 // 使用 ITime::now()
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
    void SetMouseCursor(const Rml::String& cursorName) override;
    void GetClipboardText(Rml::String& text) override;
    void SetClipboardText(const Rml::String& text) override;
    void GetSystemMetrics(...) ;                      // 屏幕尺寸/DPI
};
```

- `GetElapsedTime` 使用输入时间轴或宿主时间轴，保持单调。
- 剪贴板在 Windows 上可用 Win32 API 实现。
- `SetMouseCursor` 由宿主处理（GLFW 可设置光标）。

### 12.6 版本注意事项

RmlUi 3.x 与 4.x 的输入 API 有差异：

- 滚轮：3.x 为 `ProcessMouseWheel(int delta, ...)`；4.x 为 `ProcessMouseWheel(float delta, ...)` 或 `ProcessMouseWheel(Vector2f, ...)`。
- 修饰键枚举：4.x 增加了 `KM_CAPSLOCK/KM_NUMLOCK/KM_SCROLLLOCK`。
- 文本输入：4.x 支持 `String` 重载。

实现时用版本宏隔离：

```cpp
#if RMLUI_VERSION_MAJOR >= 4
    context->ProcessMouseWheel(float(deltaY), mods);
#else
    context->ProcessMouseWheel(int(deltaY), mods);
#endif
```

---

## 13. 自定义资产格式

### 13.1 设计原则

- JSON 作为作者格式，可读、可 diff。
- 顶层带 `version`，加载器拒绝未知大版本。
- 字段名简洁、语义明确。
- 可选：构建期烘焙为二进制（`insysb`），运行时零 JSON 解析。

### 13.2 Schema

```json
{
  "version": 1,
  "name": "default",
  "maps": [
    {
      "name": "Player",
      "actions": [
        { "name": "Move",  "type": "value", "control": "Vector2" },
        { "name": "Look",  "type": "value", "control": "Vector2" },
        { "name": "Jump",  "type": "button" },
        { "name": "Fire",  "type": "button" }
      ],
      "bindings": [
        { "action": "Move", "path": "Gamepad/LeftStick",
          "processors": "stickDeadzone(min=0.2,max=0.9)" },
        { "action": "Move", "composite": "2DVector",
          "parts": {
            "up": "Keyboard/W",
            "down": "Keyboard/S",
            "left": "Keyboard/A",
            "right": "Keyboard/D"
          } },
        { "action": "Jump", "path": "Keyboard/Space", "interactions": "tap" },
        { "action": "Jump", "path": "Gamepad/ButtonSouth" },
        { "action": "Fire", "path": "Mouse/LeftButton", "interactions": "hold(duration=0.3)" }
      ]
    },
    {
      "name": "UI",
      "actions": [
        { "name": "Navigate", "type": "value", "control": "Vector2" },
        { "name": "Submit",   "type": "button" },
        { "name": "Cancel",   "type": "button" },
        { "name": "Point",    "type": "value", "control": "Vector2" },
        { "name": "Click",    "type": "button" },
        { "name": "Scroll",   "type": "value", "control": "Vector2" }
      ],
      "bindings": [
        { "action": "Navigate", "path": "Gamepad/Dpad" },
        { "action": "Submit",   "path": "Gamepad/ButtonSouth" },
        { "action": "Cancel",   "path": "Gamepad/ButtonEast" },
        { "action": "Cancel",   "path": "Keyboard/Escape" },
        { "action": "Point",    "path": "Mouse/Position" },
        { "action": "Click",    "path": "Mouse/LeftButton" },
        { "action": "Scroll",   "path": "Mouse/Scroll" }
      ]
    }
  ],
  "schemes": [
    { "name": "Keyboard&Mouse", "bindingGroup": "Keyboard&Mouse",
      "devices": ["Keyboard", "Mouse"] },
    { "name": "Gamepad", "bindingGroup": "Gamepad", "devices": ["Gamepad"] }
  ]
}
```

### 13.3 字段说明

| 字段 | 必填 | 说明 |
|---|---|---|
| `version` | 是 | 资产版本，当前 1 |
| `name` | 否 | 资产名 |
| `maps[].name` | 是 | ActionMap 名，唯一 |
| `actions[].name` | 是 | Action 名，map 内唯一 |
| `actions[].type` | 否 | `value` / `button` / `passthrough`，默认 `button` |
| `actions[].control` | 否 | 期望控件类型，用于 UI 过滤 |
| `actions[].processors` | 否 | 默认处理器 |
| `actions[].interactions` | 否 | 默认交互 |
| `actions[].initialStateCheck` | 否 | 是否在启用时做初始状态检查 |
| `bindings[].action` | 是 | 目标 action 名 |
| `bindings[].path` | 普通绑定必填 | 控件路径 |
| `bindings[].composite` | 复合绑定必填 | 复合类型名 |
| `bindings[].parts` | 复合绑定必填 | part 名 → 路径 |
| `bindings[].interactions` | 否 | 交互列表，`;` 分隔 |
| `bindings[].processors` | 否 | 处理器列表，`;` 分隔 |
| `bindings[].groups` | 否 | 绑定组，`;` 分隔 |
| `schemes[].devices` | 是 | 设备布局名列表 |

### 13.4 加载流程

```
AssetLoader::load(path):
  1. 读取文件 → JSON
  2. 校验 version
  3. 解析 maps/actions/bindings/schemes
  4. 校验：
       - action 名唯一
       - binding.action 存在
       - composite 类型已注册
       - part 名符合 composite 要求
       - path 语法合法（可延迟到 resolve）
  5. 构建 ActionAsset
  6. （可选）烘焙二进制缓存
```

### 13.5 二进制烘焙（可选）

```
[header: magic "INSY", version, sectionCount]
[map/action/binding 表]
[string 池]
[path/processor/interaction 字符串索引]
[composite part 表]
```

- 构建期把 JSON 编译为二进制，运行时 mmap/读取即可。
- 适合发行版；开发期用 JSON 便于热重载。

---

## 14. 验收 Demo

### 14.1 技术栈

| 组件 | 选型 |
|---|---|
| 窗口/输入 | GLFW |
| 渲染上下文 | OpenGL 3.3 + glad |
| RmlUi 渲染 | 官方 `RmlUi_Renderer_GL3`（只用 Renderer，不用 Backend 输入） |
| RmlUi 系统 | `RmlUiSystemInterface`（本库） |
| RmlUi 输入 | `RmlUiInputAdapter`（本库） |

### 14.2 主循环

```cpp
int main() {
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(1280, 720, "insys acceptance", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGL();

    // RmlUi 初始化
    RmlUiSystemInterface system;
    RmlUiRendererGL3 renderer;
    Rml::SetSystemInterface(&system);
    Rml::SetRenderInterface(&renderer);
    Rml::Initialise();
    Rml::Context* context = Rml::CreateContext("main", {1280, 720});

    // 本库初始化
    insys::System sys;
    sys.initialize(window);
    insys::ui::RmlUiInputAdapter adapter;
    adapter.set_context(context);

    // 加载资产与文档
    auto asset = insys::asset::load("assets/input/default.json");
    asset.enable();
    load_documents(context);

    double last = glfwGetTime();
    double accumulator = 0.0;
    const double fixedDt = 1.0 / 60.0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double now = glfwGetTime();
        double dt = now - last; last = now;
        accumulator += dt;

        sys.update(insys::UpdateType::Dynamic);
        while (accumulator >= fixedDt) {
            sys.update(insys::UpdateType::Fixed);
            accumulator -= fixedDt;
        }

        adapter.flush();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        context->SetDimensions({w, h});
        context->Update();

        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.begin_frame(w, h);
        context->Render();
        renderer.end_frame();

        glfwSwapBuffers(window);
    }

    Rml::Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
}
```

### 14.3 验收清单

**键盘**
- A–Z、0–9、F1–F12 能正确显示到 RmlUi 文本/高亮。
- Shift 组合能输入大写与符号。
- Backspace、Enter、Tab、方向键在文本框中工作。
- 长按按键不重复触发字符（除非开启重复）。

**鼠标**
- 悬停元素高亮。
- 左/右/中键点击按钮，回调正确。
- 滚轮滚动列表。
- 指针进入/离开窗口。
- 高 DPI 下坐标正确（contentScale）。

**手柄**
- A/B/X/Y、左右摇杆、扳机、D-Pad 状态正确。
- 使用 Action 导航 RmlUi：上下左右移动焦点，A 确认，B 取消。
- 热插拔：拔插后设备列表刷新，事件不崩溃。

**Actions**
- Value / Button / PassThrough 行为正确。
- Tap / Hold / SlowTap / MultiTap 时序正确。
- 2DVector 合成 + StickDeadzone。
- 运行时重绑定后立即生效。

**UI 门控**
- UI 打开时按策略吞噬输入。
- UI 关闭后游戏输入恢复。
- 指针在 UI 上时游戏 Action 不触发（PointerAndText 模式）。

**调试面板（RmlUi 实现）**
- 设备列表与连接状态。
- 控件实时值。
- 事件流（最近 N 条）。
- Action 相位与当前控件。

---

## 15. 分阶段实施计划

### M0：骨架 + 事件/状态 + GLFW 键鼠 + 最小 Demo

交付：
- CMake 工程、`insys` 空库、`samples/acceptance` 窗口。
- `FourCC`、`Event`、`EventQueue`、`EventBuffer`、`EventStream`。
- `StateBlock`、`StateBuffers`。
- `Control`/`Device` 最小实现。
- `Manager::update` 与 `UpdateState`。
- `GlfwBackend` 键鼠回调。
- 最小 Demo：窗口 + RmlUi 一个按钮，鼠标可点击。

验收：
- 点击 RmlUi 按钮有输出。
- 键盘按下/抬起能在日志中看到。
- 单元测试：FourCC、StateBlock 读写、事件对齐。

### M1：布局系统 + 手柄

交付：
- `Layout` / `ControlItem` / `LayoutRegistry`。
- `DeviceBuilder`（含 `compute_state_layout`）。
- Keyboard/Mouse/Gamepad/Joystick 布局与状态。
- `ControlPath` 解析与匹配。
- GLFW 手柄轮询 + diff + 热插拔。
- Demo 增加手柄面板。

验收：
- 设备树正确（控件名/偏移/大小）。
- 手柄摇杆/按钮值正确。
- 热插拔不崩溃。

### M2：Actions 基础

交付：
- `Action`/`ActionMap`/`ActionAsset` 数据模型。
- `BindingResolver`。
- `ActionState`（SoA + 监视器）。
- 默认交互状态机。
- 处理器：Invert/Scale/Normalize/Clamp/Deadzone。
- 合成器：Axis/Vector2/Vector3。
- Demo 增加输入可视化。

验收：
- Value/Button/PassThrough 行为正确。
- 多绑定冲突消解正确。
- 启用/禁用正确。

### M3：交互 + 重绑定 + 资产

交付：
- Tap/Hold/SlowTap/Press/MultiTap。
- `RebindingOperation`。
- 自定义 JSON 资产加载与校验。
- ControlScheme（简化版）。
- Demo 增加重绑定页。

验收：
- 交互时序正确（单测 + 人工）。
- 重绑定后新路径生效。
- 资产加载错误有明确提示。

### M4：RmlUi 完整接入

交付：
- `RmlUiInputAdapter`（指针/键/文本/导航）。
- `RmlUiSystemInterface`。
- UI 门控策略。
- 中文 IME（可选，独立子项）。
- Demo 完成验收清单。

验收：
- 第 14.3 节全部通过。

### M5：调试工具与稳定性

交付：
- RmlUi Debugger（设备/控件/事件/Action）。
- 事件 Trace/回放。
- 单元测试覆盖率补齐。
- 性能：每帧零堆分配（事件处理/读取路径）。

验收：
- 回放测试通过。
- 长时间运行无泄漏（ASan/Valgrind）。

### M6：增强（可选）

- Win32 Raw Input（高精度鼠标/多鼠标）。
- XInput（震动/更完整手柄）。
- 更多处理器/合成器。
- 跨平台后端（Linux/macOS）。
- 二进制资产烘焙。

---

## 16. 测试策略

### 16.1 单元测试

| 模块 | 用例 |
|---|---|
| FourCC | 构造、比较、打印 |
| StateBlock | 各 format 读写、位域、边界 |
| StateBuffers | 分配、迁移、翻页 |
| ControlPath | 解析、匹配、通配符、usage |
| DeviceBuilder | 固定/自动偏移、位域分组、对齐 |
| EventStream | 原生 + append、保留事件、异常恢复 |
| ActionState | 相位迁移表、冲突消解、初始检查 |
| Interactions | Tap/Hold/SlowTap/MultiTap 时序 |
| Composites | 2DVector 各模式 |
| Processors | 各处理器数值 |
| Rebinding | 过滤、超时、路径生成 |

### 16.2 集成测试

- 假 `IRuntime` 注入事件序列，断言设备状态与 Action 回调。
- 回放录制的事件流，断言结果一致。
- 设备热插拔/禁用/重置。

### 16.3 验收测试

- Demo 人工验收（第 14.3 节）。
- 可选截图对比/自动化输入脚本。

### 16.4 性能测试

- 每帧处理 1000 个事件的耗时。
- 100 个 Action × 10 个绑定的启用/更新耗时。
- 内存：状态缓冲、ActionState arena 大小。

---

## 17. 附录 A：Unity 源码对照索引

| 模块 | 原文件 |
|---|---|
| 事件头 | `InputSystem/Runtime/Events/InputEvent.cs` |
| 状态事件 | `InputSystem/Runtime/Events/StateEvent.cs` |
| 增量事件 | `InputSystem/Runtime/Events/DeltaStateEvent.cs` |
| 文本事件 | `InputSystem/Runtime/Events/TextEvent.cs` |
| 事件缓冲 | `InputSystem/Runtime/Events/InputEventBuffer.cs` |
| 事件流 | `InputSystem/Runtime/Events/InputEventStream.cs` |
| 状态块 | `InputSystem/Runtime/State/InputStateBlock.cs` |
| 状态缓冲 | `InputSystem/Runtime/State/InputStateBuffers.cs` |
| 状态变更监视 | `InputSystem/Runtime/InputManagerStateMonitors.cs` |
| 状态接口 | `InputSystem/Runtime/State/IInputStateChangeMonitor.cs` |
| 控件基类 | `InputSystem/Runtime/Controls/InputControl.cs` |
| 控件路径 | `InputSystem/Runtime/Controls/InputControlPath.cs` |
| 布局 | `InputSystem/Runtime/Controls/InputControlLayout.cs` |
| 设备构建 | `InputSystem/Runtime/Devices/InputDeviceBuilder.cs` |
| 设备基类 | `InputSystem/Runtime/Devices/InputDevice.cs` |
| 键盘 | `InputSystem/Runtime/Devices/Keyboard.cs` |
| 鼠标 | `InputSystem/Runtime/Devices/Mouse.cs` |
| 手柄 | `InputSystem/Runtime/Devices/Gamepad.cs` |
| 摇杆 | `InputSystem/Runtime/Devices/Joystick.cs` |
| 指针 | `InputSystem/Runtime/Devices/Pointer.cs` |
| 描述/匹配 | `InputSystem/Runtime/Devices/InputDeviceDescription.cs`、`InputDeviceMatcher.cs` |
| 核心管理器 | `InputSystem/Runtime/InputManager.cs` |
| 运行时抽象 | `InputSystem/Runtime/IInputRuntime.cs`、`NativeInputRuntime.cs` |
| 更新类型 | `InputSystem/Runtime/InputUpdateType.cs` |
| 设置 | `InputSystem/Runtime/InputSettings.cs` |
| 动作 | `InputSystem/Runtime/Actions/InputAction.cs` |
| 动作表 | `InputSystem/Runtime/Actions/InputActionMap.cs` |
| 动作资产 | `InputSystem/Runtime/Actions/InputActionAsset.cs` |
| 绑定 | `InputSystem/Runtime/Actions/InputBinding.cs` |
| 动作状态 | `InputSystem/Runtime/Actions/InputActionState.cs` |
| 绑定解析 | `InputSystem/Runtime/Actions/InputBindingResolver.cs` |
| 交互上下文 | `InputSystem/Runtime/Actions/InputInteractionContext.cs` |
| 交互实现 | `InputSystem/Runtime/Actions/Interactions/*.cs` |
| 合成实现 | `InputSystem/Runtime/Actions/Composites/*.cs` |
| 处理器 | `InputSystem/Runtime/Controls/Processors/*.cs` |
| 重绑定 | `InputSystem/Runtime/Actions/InputActionRebindingExtensions.cs` |
| 控制方案 | `InputSystem/Runtime/Actions/InputControlScheme.cs` |
| UI 模块 | `InputSystem/Runtime/Plugins/UI/InputSystemUIInputModule.cs` |
| 手柄 PlayerInput | `InputSystem/Runtime/Plugins/PlayerInput/PlayerInput.cs` |

---

## 18. 附录 B：默认值与常量

| 名称 | 值 |
|---|---|
| `defaultDeadzoneMin` | 0.125 |
| `defaultDeadzoneMax` | 0.925 |
| `defaultButtonPressPoint` | 0.5 |
| `buttonReleaseThreshold` | 0.75（相对 pressPoint） |
| `defaultTapTime` | 0.2 s |
| `defaultSlowTapTime` | 0.5 s |
| `defaultHoldTime` | 0.4 s |
| `tapRadius` | 5.0 |
| `multiTapDelayTime` | 0.75 s |
| `maxQueuedEventsPerUpdate` | 1000 |
| `maxEventBytesPerUpdate` | 5 MB |
| `pollingFrequency` | 60 Hz |
| 事件最大大小 | 65535 字节 |
| 事件对齐 | 8 字节 |
| 设备 ID 0 | 无效 |

---

## 19. 附录 C：关键伪代码索引

1. `DeviceBuilder::compute_state_layout` —— 8.5
2. `Manager::update_state` —— 9.5
3. `StateChangeMonitors::process_state_change` —— 7.3
4. `BindingResolver::resolve` —— 11.2
5. `ActionState::process_control_state_change` —— 11.4
6. `ActionState::process_default_interaction` —— 11.4
7. `TapInteraction::process` —— 11.6
8. `HoldInteraction::process` —— 11.6
9. `RmlUiInputAdapter::flush` —— 12.3

---

## 20. 实施注意事项（易错点）

1. **偏移与对齐**：位域控件必须按 `compute_state_layout` 分组；非位域控件按自然对齐推进，否则 ARM 崩溃。
2. **先比较后写状态**：`UpdateState` 必须先让监视器比较旧状态与新状态，再写前台缓冲。
3. **每个设备每次 Update 只翻页一次**：依赖 `lastUpdateStepCount`。
4. **事件时间单调**：后端时间源必须与 `Manager` 一致；乱序事件会被丢弃。
5. **事件流追加**：Update 内 `QueueEvent` 必须写到 append buffer，不能递归处理。
6. **GLFW 回调唯一归属**：不要同时启用 RmlUi 的 GLFW backend 输入。
7. **RmlUi 版本差异**：滚轮/修饰键/文本输入 API 按版本隔离。
8. **手柄轮询 diff**：不要每个回调都发全量事件，避免事件洪泛。
9. **Action 回调期间禁止重新解析**：使用 defer 机制，回调结束后统一重解析。
10. **重绑定路径生成**：区分“泛化路径”（如 `*/{PrimaryAction}`）与具体路径。
11. **文本输入与按键并存**：文本框既需要 `ProcessTextInput`，也需要 `ProcessKeyDown/Up`（Backspace/Enter/方向键）。
12. **DPI**：RmlUi 坐标与 framebuffer 坐标的换算要统一，验收时专门测试高 DPI。

---

文档结束。按第 15 节里程碑从 M0 开始实现即可。
