# M0 —— 核心骨架 + 事件/状态 + GLFW 键鼠 + 最小 Demo

> 前置：无。
> 目标：搭起可运行的最小闭环——GLFW 采集键鼠 → 事件入队 → Manager 处理 → 状态更新 → RmlUi 能点击。
> 参考：`../CppInputSystem_ImplementationPlan.md` 第 5、6、7、9、10 章。

---

## 0.1 目标与交付物

完成本里程碑后，应具备：

- 可构建的 CMake 工程（核心库 + 测试 + Demo）。
- `FourCC`、事件结构、事件队列/缓冲/流。
- `StateBlock` 读写、`StateBuffers` 双缓冲。
- 最小 `Control` / `Device`（Keyboard、Mouse，暂时用代码内布局）。
- `Manager`：添加设备、`Update`、`ProcessEvent`、`UpdateState`。
- `IRuntime` 抽象与 `GlfwBackend`（仅键鼠）。
- 最小 Demo：GLFW 窗口 + RmlUi 一个按钮，可点击；键盘事件打印。

---

## 0.2 任务清单

- [x] 0.2.1 工程与构建
- [x] 0.2.2 基础类型（FourCC / align / Settings / UpdateType）
- [x] 0.2.3 事件结构
- [x] 0.2.4 EventQueue / EventBuffer / EventStream
- [x] 0.2.5 StateBlock 读写
- [x] 0.2.6 StateBuffers
- [x] 0.2.7 最小 Control / Device
- [x] 0.2.8 Manager 核心
- [x] 0.2.9 IRuntime + GlfwBackend
- [x] 0.2.10 最小 Demo
- [x] 0.2.11 单元测试

---

## 0.3 步骤详解

### 0.3.1 工程与构建

创建：

```
insys/
  CMakeLists.txt
  include/insys/...
  src/...
  tests/CMakeLists.txt
  samples/acceptance/CMakeLists.txt
```

`CMakeLists.txt` 要点：

```cmake
cmake_minimum_required(VERSION 3.20)
project(insys LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(INSYS_BUILD_TESTS "Build tests" ON)
option(INSYS_BUILD_DEMO  "Build acceptance demo" ON)

add_library(insys STATIC
  src/core/fourcc.cpp
  src/core/event.cpp
  src/core/event_queue.cpp
  src/core/event_buffer.cpp
  src/core/event_stream.cpp
  src/core/state_block.cpp
  src/core/state_buffers.cpp
  src/core/control.cpp
  src/core/device.cpp
  src/core/manager.cpp
  src/core/system.cpp
)
target_include_directories(insys PUBLIC include)
target_compile_options(insys PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)

if (INSYS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if (INSYS_BUILD_DEMO)
  add_subdirectory(samples/acceptance)
endif()
```

后续里程碑按需把 `src/actions/*`、`src/backend/glfw/*`、`src/ui/rmlui/*` 加入目标（M0 也可先建空文件）。

验收：`cmake -S insys -B build && cmake --build build` 成功，`ctest` 可运行（初始 0 个测试也接受）。

---

### 0.3.2 基础类型

文件：

- `include/insys/update_type.h`
- `include/insys/settings.h`
- `include/insys/low/fourcc.h`

实现参考总体设计 5.1、5.3、5.4。

`fourcc.h`：

```cpp
namespace insys {
struct FourCC {
    uint32_t value = 0;
    constexpr FourCC() = default;
    constexpr explicit FourCC(uint32_t v) : value(v) {}
    constexpr FourCC(char a, char b, char c, char d = ' ')
        : value((uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
                (uint32_t(uint8_t(c)) << 8)  |  uint32_t(uint8_t(d))) {}
    constexpr bool operator==(const FourCC& o) const { return value == o.value; }
    constexpr bool operator!=(const FourCC& o) const { return value != o.value; }
};
}
```

`update_type.h`：

```cpp
namespace insys {
enum class UpdateType : uint32_t {
    None = 0,
    Dynamic = 1u << 0,
    Fixed = 1u << 1,
    BeforeRender = 1u << 2,
    Manual = 1u << 3,
    Default = Dynamic | Fixed,
};
using UpdateMask = uint32_t;
inline bool has_update(UpdateMask m, UpdateType t) {
    return (m & static_cast<uint32_t>(t)) != 0;
}
}
```

`settings.h`：使用总体设计 5.4 的字段与默认值。

测试：

- FourCC 构造/比较。
- `has_update` 组合。
- 默认值与总体设计一致。

---

### 0.3.3 事件结构

文件：

- `include/insys/low/event.h`
- `src/core/event.cpp`

按总体设计 6.1、6.2 实现：

```cpp
namespace insys::low {
constexpr uint32_t kEventAlignment = 8;
constexpr uint32_t kHeaderSize = 24;

struct Event {
    uint32_t type;
    uint16_t size;
    uint16_t deviceId;
    uint32_t eventId;
    uint32_t _pad0;
    double   time;
};
static_assert(sizeof(Event) == kHeaderSize);

struct StateEvent {
    Event header;
    FourCC stateFormat;
    uint8_t stateData[1];
};

struct DeltaStateEvent {
    Event header;
    FourCC stateFormat;
    uint32_t stateOffset;
    uint8_t deltaState[1];
};

struct TextEvent { Event header; int32_t character; };
struct DeviceRemoveEvent { Event header; };
struct DeviceConfigurationEvent { Event header; };
struct DeviceResetEvent { Event header; bool hardReset; uint8_t _pad[3]; };

Event make_event(FourCC type, uint16_t size, int deviceId, double time);
}
```

注意：

- `size` 含头部，向上对齐到 4（建议 8）。
- `eventId` 最高位作为 handled。
- 所有工厂函数返回 `std::vector<uint8_t>`，由调用方决定入队。

测试：

- `sizeof(Event) == 24`。
- `next_in_memory` 按 8 字节对齐前进。
- handled 位读写正确。

---

### 0.3.4 EventQueue / EventBuffer / EventStream

文件：

- `include/insys/low/event_buffer.h`
- `include/insys/low/event_queue.h`
- `include/insys/low/event_stream.h`
- 对应 `src/core/*.cpp`

`EventBuffer`：

```cpp
class EventBuffer {
public:
    EventBuffer() = default;
    explicit EventBuffer(std::vector<uint8_t> data);
    int event_count() const;
    uint32_t size_bytes() const;
    uint32_t capacity_bytes() const;
    uint8_t* data();
    const uint8_t* data() const;
    Event* begin();
    Event* end();
    void reset();
    Event* allocate(int sizeBytes);
    void append(const Event* e);
    bool contains(const Event* e) const;
};
```

`EventQueue`（线程安全）：

```cpp
class EventQueue {
public:
    explicit EventQueue(size_t capacityBytes = 1 << 20);
    void push(const Event* e);          // 后端线程
    EventBuffer drain();                // 主线程
    void clear();
private:
    std::mutex m_mutex;
    std::vector<uint8_t> m_pending;
};
```

`EventStream`：

```cpp
class EventStream {
public:
    EventStream(EventBuffer& nativeBuffer, int maxAppendedEvents);
    bool is_open() const;
    int remaining() const;
    Event* current() const;
    Event* peek() const;
    Event* advance(bool keepInBuffer);
    void write(Event* e);
    void close(EventBuffer& out);
    void cleanup_after_exception();
private:
    EventBuffer m_native;
    EventBuffer m_append;
    Event* m_nativeRead = nullptr;
    Event* m_nativeWrite = nullptr;
    int m_remainingNative = 0;
    Event* m_appendRead = nullptr;
    Event* m_appendWrite = nullptr;
    int m_remainingAppend = 0;
    int m_maxAppended = 0;
    bool m_open = false;
};
```

实现要点（参考 `InputEventStream.cs`）：

- `advance(false)` 正常前进；`advance(true)` 把当前事件保留在缓冲（用于时间切片/BeforeRender）。
- 保留事件时，把剩余事件紧凑到缓冲前部。
- `write` 追加到 `m_append`，不能在遍历原生事件时修改原生缓冲。
- `close` 把保留的事件写回 `out`。

测试：

- 压入 3 个事件，遍历顺序正确。
- 遍历中 `write` 的事件在原生事件之后被处理。
- `advance(true)` 保留事件，下次 `close` 后仍在缓冲。
- 超过 `maxAppended` 时的行为（丢弃并记录）。

---

### 0.3.5 StateBlock

文件：`include/insys/state_block.h`、`src/core/state_block.cpp`。

按总体设计 7.1 实现格式常量与读写。

```cpp
struct StateBlock {
    static constexpr uint32_t InvalidOffset = 0xFFFFFFFFu;
    static constexpr uint32_t AutomaticOffset = 0xFFFFFFFEu;

    FourCC format{};
    uint32_t byteOffset = InvalidOffset;
    uint32_t bitOffset = 0;
    uint32_t sizeInBits = 0;

    uint32_t aligned_size_in_bytes() const;
    uint32_t effective_byte_offset() const;
};

int32_t read_int(const StateBlock&, const void* state);
void write_int(const StateBlock&, void* state, int32_t);
float read_float(const StateBlock&, const void* state);
void write_float(const StateBlock&, void* state, float);
bool read_bit(const StateBlock&, const void* state);
void write_bit(const StateBlock&, void* state, bool);
```

位域工具（`src/core/bit_math.h`）：

```cpp
bool read_single_bit(const uint8_t* ptr, uint32_t bitOffset);
void write_single_bit(uint8_t* ptr, uint32_t bitOffset, bool value);
uint32_t read_multiple_bits(const uint8_t* ptr, uint32_t bitOffset, uint32_t count);
void write_multiple_bits(uint8_t* ptr, uint32_t bitOffset, uint32_t count, uint32_t value);
float read_normalized(const uint8_t* ptr, uint32_t bitOffset, uint32_t count);
void write_normalized(uint8_t* ptr, uint32_t bitOffset, uint32_t count, float value);
```

测试：

- 各 format 读写往返。
- 1-bit、2-bit、多 bit 位域。
- 跨字节位域（如 bitOffset=6, count=4）。
- 归一化边界（0/1、-1/1）。

---

### 0.3.6 StateBuffers

文件：`include/insys/state_buffers.h`、`src/core/state_buffers.cpp`。

按总体设计 7.2 实现最小版本：

```cpp
class StateBuffers {
public:
    void allocate_all(const std::vector<Device*>& devices);
    void free_all();

    void switch_to(UpdateType type);
    uint8_t* front_for(int deviceIndex);
    uint8_t* back_for(int deviceIndex);
    void swap(int deviceIndex);

    std::vector<uint8_t> defaultState;
    std::vector<uint8_t> noiseMask;
    std::vector<uint8_t> resetMask;

private:
    struct DoubleBuffers {
        std::vector<uint8_t> front, back;
        bool valid = false;
    };
    DoubleBuffers m_player;
    DoubleBuffers m_editor;   // M0 可留空
    UpdateType m_current = UpdateType::None;
};
```

M0 简化：

- 只实现一套 player 双缓冲（Dynamic/Fixed/BeforeRender 共用）。
- 每个设备按 `aligned_size_in_bytes()` 向上对齐到 4。
- 迁移逻辑可先留空（M1 再实现），但接口保留。

测试：

- 分配后各设备偏移不重叠。
- `swap` 后 front/back 互换。
- `defaultState/noiseMask/resetMask` 大小正确。

---

### 0.3.7 最小 Control / Device

文件：

- `include/insys/control.h`、`src/core/control.cpp`
- `include/insys/device.h`、`src/core/device.cpp`

M0 只需要能构造“手工布局”的 Keyboard/Mouse：

```cpp
class Control {
public:
    virtual ~Control() = default;

    InternedString name;
    InternedString layout;
    Device* device = nullptr;
    Control* parent = nullptr;

    uint32_t childStart = 0, childCount = 0;
    StateBlock stateBlock;
    uint32_t flags = 0;

    std::vector<std::unique_ptr<IProcessor>> processors;

    virtual size_t value_size() const = 0;
    virtual void read_value_from_state(const void* state, void* out) const = 0;
    virtual void write_value_to_state(void* state, const void* in) const = 0;
    virtual float evaluate_magnitude(const void* state) const { return 0.f; }
};
```

M0 具体控件：

- `ButtonControl`（float，`pressPoint`，`wasPressedThisFrame` 简化）。
- `AxisControl`（float）。
- `Vector2Control`（Vector2）。
- `KeyControl`（Button + 键码）。

`Device`：

```cpp
class Device : public Control {
public:
    int deviceId = 0;
    int deviceIndex = -1;
    DeviceDescription description;
    uint32_t deviceFlags = 0;
    double lastUpdateTime = 0;
    uint32_t lastUpdateStepCount = 0;

    std::vector<Control*> allControls;      // 含自身
    std::vector<Control*> childrenFlat;     // 由 builder 填充
};
```

M0 手工构建设备：

```cpp
std::unique_ptr<Device> make_keyboard(int deviceId);
std::unique_ptr<Device> make_mouse(int deviceId);
```

- Keyboard：`KeyboardState` 位域，每个键一个 `KeyControl`（至少实现 A–Z、0–9、Space、Enter、Escape、Shift/Ctrl/Alt）。
- Mouse：`MouseState`，`position/delta/scroll/leftButton/rightButton/middleButton`。

> M1 会用通用 `DeviceBuilder` 替换这些手工构造函数。M0 的控件结构要尽量贴近最终布局，减少返工。

测试：

- 构建 Keyboard 后 `allControls` 数量正确。
- `KeyControl` 读写对应 bit。
- Mouse 按钮位读写正确。

---

### 0.3.8 Manager 核心

文件：`include/insys/manager.h`、`src/core/manager.cpp`。

M0 只实现：

```cpp
class Manager {
public:
    Manager(IRuntime& runtime, const Settings& settings);
    ~Manager();

    const std::vector<Device*>& devices() const;
    Device* add_device(std::unique_ptr<Device> device);
    void remove_device(Device* device);
    Device* device_by_id(int id);

    void queue_event(const Event* e);
    void update(UpdateType type);
    void update();

    bool update_state(Device* device, const Event* stateEvent, UpdateType type);
    bool update_state(Device* device, UpdateType type,
                      const void* state, uint32_t offset, uint32_t size,
                      double time, const Event* e = nullptr);

    StateChangeMonitors& state_monitors();
    StateBuffers& state_buffers();

private:
    IRuntime* m_runtime;
    Settings m_settings;
    std::vector<std::unique_ptr<Device>> m_owned;
    std::vector<Device*> m_devices;
    std::unordered_map<int, Device*> m_byId;
    UpdateMask m_updateMask;
    UpdateType m_currentUpdate = UpdateType::None;
    StateBuffers m_stateBuffers;
    StateChangeMonitors m_stateMonitors;
    EventQueue m_queue;
    EventStream* m_stream = nullptr;
};
```

`update` 流程见总体设计 9.4。M0 可先不实现时间切片、合并、预处理器，保留 TODO。

`update_state` 必须实现：

1. 监视器比较旧/新状态（M0 可先实现最简：只要有监视器就置位）。
2. 需要时翻页。
3. 写入前台缓冲。
4. 更新 `lastUpdateTime`。
5. `make_current`（M0 可只记录）。

`flip_buffers_for_device_if_necessary`：

```cpp
bool flip_if_needed(Device* d) {
    if (d->lastUpdateStepCount == m_updateStepCount) return false;
    m_stateBuffers.swap(d->deviceIndex);
    d->lastUpdateStepCount = m_updateStepCount;
    return true;
}
```

M0 需要维护一个全局 `m_updateStepCount`，每次 `update` 递增。

测试：

- 添加设备后 `stateBuffers` 已分配。
- 发送 StateEvent 后 `device.currentStatePtr` 值正确。
- 同一 Update 多次状态事件不会重复翻页。

---

### 0.3.9 IRuntime + GlfwBackend

文件：

- `include/insys/backend/iruntime.h`
- `include/insys/backend/iwindow.h`
- `include/insys/backend/itime.h`
- `include/insys/backend/glfw_backend.h`
- `src/backend/glfw/glfw_backend.cpp`
- `src/backend/glfw/glfw_key_map.cpp`

`IRuntime` 见总体设计 10.1。M0 需要：

```cpp
class Runtime final : public IRuntime {
public:
    int allocate_device_id() override;
    void queue_event(const Event* e) override;
    long device_command(int deviceId, DeviceCommand* cmd) override;
    double now() const override;
    double now_for_fixed() const override;
    float unscaled_game_time() const override;
    Vector2 screen_size() const override;
    int screen_orientation() const override;
    bool run_in_background() const override;
    void set_run_in_background(bool) override;
    FocusState focus_state() const override;
    void set_focus_state(FocusState) override;
    float polling_frequency() const override;
    void set_polling_frequency(float) override;

    std::function<void(UpdateType, EventBuffer&)> on_update;
    // ... 其他回调
private:
    int m_nextDeviceId = 1;
    Manager* m_manager = nullptr;  // 或通过回调
    EventQueue* m_queue = nullptr;
};
```

> 简化：`Runtime::queue_event` 直接调用 `Manager::queue_event`（通过回调），避免双向依赖。

`GlfwBackend`：

```cpp
class GlfwBackend {
public:
    bool initialize(GLFWwindow* window, Runtime& runtime, Manager& manager);
    void shutdown();
    void poll();     // 每帧：轮询手柄（M1）、处理窗口焦点
private:
    static void key_callback(GLFWwindow*, int key, int scancode, int action, int mods);
    static void char_callback(GLFWwindow*, unsigned int codepoint);
    static void cursor_callback(GLFWwindow*, double x, double y);
    static void mouse_button_callback(GLFWwindow*, int button, int action, int mods);
    static void scroll_callback(GLFWwindow*, double x, double y);

    void emit_keyboard();
    void emit_mouse();
    void emit_text(uint32_t codepoint);
};
```

M0 映射：

- 按键：GLFW → 内部 `KeyCode`，维护 `KeyboardState` 位，产生 StateEvent。
- 鼠标：维护 `MouseState`（position/delta/buttons/scroll），产生 StateEvent 或 DeltaStateEvent。
- 文本：`char_callback` → TextEvent。

注意：

- GLFW 回调是静态函数，需要 `glfwSetWindowUserPointer(window, this)` 获取实例。
- 不要在回调里直接处理事件，只更新状态并入队。
- 鼠标 delta 用 `position - lastPosition`。

验收：

- 点击 RmlUi 按钮时，日志能看到鼠标按下/抬起。
- 输入文字时，日志能看到 TextEvent。

---

### 0.3.10 最小 Demo

文件：

- `samples/acceptance/main.cpp`
- `samples/acceptance/renderer_gl3.*`（可直接链接 RmlUi 官方 Renderer_GL3）
- `samples/acceptance/assets/ui/main.rml`、`main.rcss`

实现总体设计 14.2 的主循环。M0 只需要：

- 一个 RmlUi 按钮，点击后 `std::cout << "clicked\n"`。
- 一个文本输入框，能接收字符。
- 键盘/鼠标事件日志（可开关）。

`RmlUiSystemInterface` 与 `RmlUiInputAdapter` 在 M0 可先写最小版本：

- `RmlUiSystemInterface`：`GetElapsedTime`、`LogMessage`、`GetSystemMetrics`。
- `RmlUiInputAdapter`：只处理鼠标移动/按键/滚轮与键盘/文本。

> 完整版在 M4 实现，M0 的适配器只需够 Demo 验收。

验收：

- 窗口打开，RmlUi 按钮可见。
- 鼠标点击按钮输出 `clicked`。
- 键盘输入字符进入文本框。

---

### 0.3.11 单元测试

`tests/` 下至少：

- `test_fourcc.cpp`
- `test_state_block.cpp`
- `test_event.cpp`
- `test_event_stream.cpp`
- `test_state_buffers.cpp`

使用 Catch2 或 GoogleTest。`ctest` 全绿。

---

## 0.4 验收标准

- [x] 构建成功，无警告。
- [x] `ctest` 全绿。
- [x] Demo 窗口正常，RmlUi 按钮可点击。
- [x] 键盘按下/抬起、字符输入有日志。
- [x] 鼠标移动/点击有日志。
- [ ] ASan/Valgrind 无泄漏（Windows 可用 `/fsanitize=address` 或 Dr. Memory；当前仅做了常规运行验证，未执行内存检查）。

---

## 0.5 常见问题

1. **事件 size 不是 4/8 的倍数**：导致后续事件错位。工厂函数里统一对齐。
2. **同一帧多次状态事件导致翻页多次**：必须用 `lastUpdateStepCount` 控制。
3. **GLFW 回调里直接处理**：会与主循环竞态；只入队。
4. **鼠标 delta 计算错误**：首次移动时 lastPosition 未初始化，需跳过或置零。
5. **RmlUi 坐标不对**：M0 先用窗口坐标，M4 再处理 contentScale/DPI。

---

## 0.6 完成后的下一步

进入 [M1-Layout-Gamepad.md](M1-Layout-Gamepad.md)：用通用布局系统替换 M0 的手工设备构造，并加入手柄支持。
