# M6 —— 增强项（可选）

> 前置：M5 完成。
> 目标：在稳定核心之上扩展平台能力与工具链。按需选择，不阻塞主线。
> 参考：`../CppInputSystem_ImplementationPlan.md` 第 10.3、13.5 章。

---

## 6.1 可选项清单

- [x] 6.1.1 Win32 Raw Input（高精度鼠标、多鼠标）
- [x] 6.1.2 XInput 后端（震动、更完整手柄）
- [x] 6.1.3 核心处理器/合成器（CompensateDirection/Rotation、OneModifier/TwoModifiers/ButtonWith*）
- [x] 6.1.3 Android 传感器补偿处理器（AndroidCompensateDirection/Rotation）
- [x] 6.1.3 扩展项：自定义死区曲线（AxisDeadzoneCurve / StickDeadzoneCurve）
- [x] 6.1.3 扩展项：基础手势识别（Tap/Swipe/LongPress/Pinch）
- [x] 6.1.3 扩展项：平滑/滤波（Smooth / SmoothVector2 / SmoothVector3）
- [ ] 6.1.4 跨平台后端（Linux/macOS）
- [x] 6.1.4 Linux evdev 映射核心（键盘/鼠标/手柄）
- [x] 6.1.4 Linux evdev 状态聚合（frame dirty）
- [x] 6.1.4 Linux evdev 事件解码/能力位分类
- [x] 6.1.4 Linux evdev 采集会话（多设备、SYN_REPORT/脏帧）
- [x] 6.1.4 Linux evdev 热插拔设备差异跟踪
- [x] 6.1.4 Linux evdev 后端集成（设备创建/状态事件）
- [x] 6.1.4 Linux `/dev/input` 扫描与 read 源
- [x] 6.1.4 Linux inotify 事件解码与扫描触发
- [x] 6.1.4 Linux inotify watcher（/dev/input）
- [ ] 6.1.4 Linux evdev 实机编译/运行验证
- [x] 6.1.4 macOS GameController 映射核心（手柄按钮/轴/扳机）
- [x] 6.1.4 macOS GameController 后端集成（设备/状态事件）
- [ ] 6.1.4 Linux evdev 设备采集与 macOS GameController 采集
- [x] 6.1.5 二进制资产烘焙
- [x] 6.1.6 核心设备：Touchscreen（10 指）、Gyroscope/Accelerometer/Gravity/LinearAcceleration/MagneticField/Attitude
- [x] 6.1.6 基础控件：Integer / Delta / Double / DiscreteButton
- [x] 6.1.6 环境传感器：Light / Pressure / Proximity / Humidity / AmbientTemperature / StepCounter / HingeAngle
- [x] 6.1.6 Pen（pressure/tilt/twist）与 TrackedDevice
- [x] 6.1.6 iOS CMPedometer 计步器适配
- [x] 6.1.6 Windows 触摸后端：`WM_TOUCH` → `Touchscreen` 状态事件
- [x] 6.1.6 传感器后端集成（Gyro/Accel/Gravity/LinearAcceleration/MagneticField/Attitude/Light/Pressure/Proximity/Humidity/AmbientTemperature/StepCounter/HingeAngle）
- [x] 6.1.6 触摸后端集成核心（10 指/帧合并）
- [x] 6.1.6 移动端输入桥（触摸+运动传感器）
- [x] 6.1.6 移动端跨线程事件队列
- [x] 6.1.6 移动端 suspend 触点取消
- [x] 6.1.6 移动端触摸压力透传
- [x] 6.1.6 Android 传感器事件适配（AndroidSensorType → MobileInputBridge）
- [x] 6.1.6 iOS CoreMotion 采样适配（Gyro/Accel/DeviceMotion）
- [x] 6.1.6 iOS CoreMotion 平台回调（CMMotionManager，主线程）
- [x] 6.1.6 Android JNI 触摸/传感器入口（MobileInput.java）
- [x] 6.1.6 Android Java 侧 SensorEventListener/MotionEvent 适配（AndroidInput.java）
- [x] 6.1.6 iOS 触摸适配（UIGestureRecognizer → MobileInputBridge）
- [ ] 6.1.6 其他平台后端：evdev/libinput、GameController
- [x] 6.1.7 远程输入核心：消息协议、设备镜像、事件转发（LoopbackTransport）
- [x] 6.1.7 TCP 传输：`TcpServer` / `TcpTransport`（IPv4，非阻塞）
- [x] 6.1.7 UDP 传输：`UdpTransport`（IPv4，best-effort）
- [x] 6.1.7 WebSocket 传输：`WebSocketTransport`（RFC6455，WS，无 TLS）
- [x] 6.1.8 EnhancedTouch：`Touch` / `Finger` / `TouchHistory` / `TouchSimulation`
- [x] 6.1.8 OnScreen：`UiPointerInputModule`、`OnScreenButton`、`OnScreenStick`

---

## 6.2 6.1.1 Win32 Raw Input

### 目标

- 获取不受系统指针加速影响的原始鼠标 delta。
- 支持多鼠标设备（每个物理鼠标一个 Device）。
- 支持高轮询率鼠标。

### 实现

1. 在 GLFW 窗口的 `WndProc` 中处理 `WM_INPUT`：

```cpp
case WM_INPUT: {
    UINT size = 0;
    GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    std::vector<BYTE> buffer(size);
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer.data(), &size,
                        sizeof(RAWINPUTHEADER)) == size) {
        auto* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
        if (raw->header.dwType == RIM_TYPEMOUSE) {
            // raw->data.mouse.lLastX / lLastY 是原始 delta
        }
    }
    break;
}
```

2. 注册原始输入：

```cpp
RAWINPUTDEVICE rid{};
rid.usUsagePage = 0x01;   // Generic Desktop
rid.usUsage = 0x02;       // Mouse
rid.dwFlags = RIDEV_INPUTSINK;
rid.hwndTarget = hwnd;
RegisterRawInputDevices(&rid, 1, sizeof(rid));
```

3. 产生 `DeltaStateEvent` 更新 `MouseState.delta`。
4. 多鼠标：按 `raw->header.hDevice` 区分，创建独立 Mouse 设备。

### 集成

- 作为 `IPlatformBackend` 的补充：`GlfwBackend` 仍负责光标位置与按钮，`RawInputBackend` 负责 delta。
- 通过配置开关启用：`Settings::useRawInput`。

### 验收

- 原始 delta 与系统光标行为解耦（关闭指针加速后一致）。
- 多鼠标可区分。

---

## 6.3 6.1.2 XInput 后端

### 目标

- 支持震动（rumble）。
- 更完整的手柄按钮/扳机映射（GLFW 的 gamepad 映射有限）。
- 支持 4 个 XInput 手柄。

### 实现

```cpp
#include <XInput.h>
#pragma comment(lib, "xinput.lib")

void XInputBackend::poll() {
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE state{};
        if (XInputGetState(i, &state) != ERROR_SUCCESS) continue;

        GamepadState gs = map_xinput(state.Gamepad);
        // diff → DeltaStateEvent
    }
}

void XInputBackend::set_rumble(int slot, float low, float high) {
    XINPUT_VIBRATION vib{
        (WORD)(std::clamp(low, 0.f, 1.f) * 65535),
        (WORD)(std::clamp(high, 0.f, 1.f) * 65535)
    };
    XInputSetState(slot, &vib);
}
```

- 设备命令：实现 `GetHapticCapabilitiesCommand` / `SendHapticImpulseCommand` / `SendBufferedHapticsCommand`（本库自定义命令）。
- 与 GLFW 手柄二选一，避免同一物理手柄创建两个设备：优先 XInput，GLFW 只处理非 XInput 手柄。

### 验收

- 手柄按钮/摇杆/扳机正确。
- 震动命令生效。
- 不重复创建设备。

---

## 6.4 6.1.3 更多处理器/合成器/交互

按需补充：

**处理器**
- `CompensateDirection` / `CompensateRotation`
- `ScaleVector2/3` 的变体
- 自定义死区曲线
- 平滑/滤波（`Smooth` / `SmoothVector2` / `SmoothVector3`，按 `halfLife` 时间常数）

**合成器**
- `ButtonWithOneModifier` / `ButtonWithTwoModifiers`
- `OneModifier` / `TwoModifiers`
- 自定义方向合成

**交互**
- `Press` 的完整行为
- 手势识别（基础 Tap/Swipe/LongPress/Pinch 已完成）

实现方式：

- 注册表 + 工厂。
- 参数通过 `set_parameter`。
- 单元测试覆盖。

### 已完成（核心项）

参考 Unity：

- `CompensateDirectionProcessor.cs`
- `CompensateRotationProcessor.cs`
- `OneModifierComposite.cs`
- `TwoModifiersComposite.cs`
- `ButtonWithOneModifier.cs`
- `ButtonWithTwoModifiers.cs`

已实现：

- `CompensateDirection`
  - `Vector3` 输入，按屏幕方向绕 Z 轴旋转。
  - `Unknown/Portrait` 保持原值；`PortraitUpsideDown` 旋转 180°；
    `LandscapeLeft` 旋转 90°；`LandscapeRight` 旋转 270°。
  - 仅在 `Settings::compensateForScreenOrientation` 为 `true` 时生效。
  - `CachingPolicy::EvaluateOnEveryRead`，屏幕方向变化后立即生效。
- `CompensateRotation`
  - `Quaternion` 输入，按 Unity 的四元数补偿值右乘。
  - 使用 `Settings::compensateForScreenOrientation` 与 `IRuntime::screen_orientation()`。
- `AndroidCompensateDirection` / `AndroidCompensateRotation`
  - 对齐 Unity `AndroidSensors.cs` 的 `AndroidCompensateDirectionProcessor` /
    `AndroidCompensateRotationProcessor`。
  - 方向值先乘 `-1 / 9.80665`（m/s² 转 g），再执行屏幕方向补偿；
    `compensateForScreenOrientation=false` 时仍做单位转换。
  - 旋转向量按 `w = sqrt(max(0, 1 - x² - y² - z²))` 重建四元数，
    再执行屏幕方向补偿；`w=0` 表示超出单位球。
  - 两者均为 `EvaluateOnEveryRead`，单测覆盖单位换算、四种屏幕方向和关闭补偿。
- `OneModifier` / `TwoModifiers`
  - 支持 `modifier`、`modifier1`、`modifier2`、`binding` 和 `button` 部件名。
  - 动态读取 `binding` 部件的值类型和大小；`ButtonWith*` 固定为 `float`。
  - 支持 `modifiersOrder=0/1/2`（Default/Ordered/Unordered）以及
    兼容参数 `overrideModifiersNeedToBePressedFirst`。
  - Ordered 模式用各部件 `pressTime` 判断 modifier 是否先于 button 按下。
  - Default 模式跟随 `Settings::shortcutKeysConsumeInput`：默认 `false` 时为
    Unordered，开启后为 Ordered。
- `AxisDeadzone` / `StickDeadzone` 扩展
  - 参考 Unity `AxisDeadzoneProcessor.cs` / `StickDeadzoneProcessor.cs` 的 min/max
    归一化，再增加可选的分段线性响应曲线。
  - `curve="0,0;0.5,0.25;1,1"`（也接受 `:` 分隔点），x/y 范围 `[0,1]`，
    区间外按端点钳制；未提供或非法曲线保持 Unity 原始线性行为。
  - 别名 `AxisDeadzoneCurve` / `StickDeadzoneCurve`；标量保留符号，摇杆按幅度
    缩放，曲线对幅度生效。
- `Smooth` / `SmoothVector2` / `SmoothVector3`
  - 参考 Unity `InputUserSettings.mouseSmoothing` 的可配置平滑意图；Unity 核心
    没有同名处理器，本项目按统一处理器接口扩展。
  - 参数 `halfLife`（秒，默认 `0.1`），使用 `IRuntime::now()` 计算帧间隔并按
    指数衰减插值；`halfLife=0` 时直通。
  - 处理器有状态且 `CachingPolicy::EvaluateOnEveryRead`，每个 binding 使用独立实例；
    无 `ProcessorContext` 时直通。
- 处理器上下文
  - `ProcessorContext` 携带 `Settings` 和 `IRuntime`，由 `ActionState` 在读取绑定时注入。
  - 保留原有三参数 `IProcessor::process`，不破坏已有处理器。
- 手势识别
  - `TouchGestureRecognizer` 消费 `TouchscreenState` 和时间轴，输出 Tap、LongPress、
    Swipe（Up/Down/Left/Right）和双指 Pinch 事件。
  - 参数可配置 tap 距离/时长、swipe 距离、long press 时长和 pinch 缩放阈值；
    多指触摸时抑制单指 swipe/tap，避免与 Pinch 冲突。
  - 纯用户层工具，不依赖平台触摸后端。

扩展项平滑/滤波已完成。`Press` 已有 PressOnly/ReleaseOnly/PressAndRelease 行为，
本次只做对齐审计，未新增状态。

---

## 6.5 6.1.4 跨平台后端

### Linux

- 已实现平台无关的 `evdev_mapping`：
  - `map_key` 覆盖 Linux KEY_* 到内部 `Key`。
  - `MouseMapper` 处理 `REL_X/Y`、滚轮/横滚和 `BTN_LEFT/RIGHT/MIDDLE/SIDE/EXTRA`。
  - `GamepadMapper` 处理 ABS 摇杆/扳机归一化、`BTN_SOUTH/EAST/NORTH/WEST`、
    shoulder/start/select/thumb 和 `ABS_HAT0X/Y`/`BTN_DPAD_*`。
  - 映射逻辑可在非 Linux 平台单测，见 `tests/test_evdev_mapping.cpp`。
  - `EvdevDeviceState` 聚合键盘位图/MouseState/GamepadState，按 SYN_REPORT 外部
    调用 `end_frame()` 判断是否需要发送状态事件；见 `tests/test_evdev_device.cpp`。
  - `EvdevEventDecoder` 从原始 `input_event` 字节流解码 type/code/value，支持
    64/32 位布局和跨读取边界缓存；`decode_evdev_capabilities` 解析 ioctl 能力位，
    `classify_evdev_device` 分类键盘/鼠标/手柄；见 `tests/test_evdev_reader.cpp`。
  - `EvdevCaptureSession` 管理多个设备及其 decoder/state，轮询可注入的读取函数，
    在 `SYN_REPORT` 后返回脏设备；见 `tests/test_evdev_capture.cpp`。
  - `EvdevDeviceTracker` 对比设备扫描结果，输出 Added/Removed 差异并处理类型变化；
    见 `tests/test_evdev_hotplug.cpp`。
  - `EvdevBackend` 将扫描/热插拔、采集会话与 `Manager` 集成：按设备创建
    `Keyboard`/`Mouse`/`Gamepad`，脏帧编码为状态事件，移除时删除设备；
    见 `tests/test_evdev_backend.cpp`。
  - `linux_evdev`（`include/insys/backend/linux_evdev.h`）扫描 `/dev/input/event*`，
    用 `EVIOCGBIT` 查询能力并分类；source 打开非阻塞 fd，按 `read()` 结果
    归一化为 24 字节记录，供跨架构 decoder 使用。
  - `InotifyEventDecoder`（`include/insys/backend/inotify_reader.h`）解析
    `inotify_event` 字节流（含 name 填充/半包），`is_input_scan_event` 覆盖
    create/delete/move/attrib/q-overflow/ignored。
  - `InputHotplugMonitor` 过滤 `event*` 设备变化并合并扫描请求；
    `LinuxInotifyWatcher` 在 `/dev/input` 上建立非阻塞 watch 并读取事件。
- 待做：watch 丢失后的自动重建、真实 Linux 实机编译/运行验证；libinput 可作为
  可选后端。
- GLFW 作为回退；可选 SDL3 后端（手柄/HID 更好）。

### macOS

- 已实现平台无关的 `gamecontroller_mapping`：
  - `GameControllerState` 表示 `GCExtendedGamepad` 常用字段。
  - `map_gamecontroller_state` 映射 A/B/X/Y、shoulder、trigger、左右摇杆、
    dpad、menu/options 和摇杆按键到 `GamepadState`，轴/扳机做钳制。
  - 可在非 macOS 平台单测，见 `tests/test_gamecontroller_mapping.cpp`。
- 已实现 `GameControllerBackend`：控制器增删、创建 `Gamepad` 设备、脏状态
  合并和 `poll()` 状态事件；见 `tests/test_gamecontroller_backend.cpp`。
- 待做：`GameController.framework` 设备发现/连接/回调、IOHIDManager 原始鼠标 delta。
  M7 已补充 `MacOsHid` 的 IOHIDManager 手柄枚举/读取/写入，但仍未在 macOS 编译或实机验证。
- GLFW 可用；当前环境无法编译/运行 macOS 后端。

### 抽象

- 保持 `IPlatformBackend` 接口不变。
- 每个平台一个目录：`src/backend/linux/`、`src/backend/macos/`。
- 通过 CMake 选择。

验收：

- Linux evdev 映射/状态聚合/事件解码/能力位分类/采集会话/热插拔差异/后端集成、Linux 平台源/inotify watcher、macOS GameController 映射/后端、传感器后端、触摸后端和移动端桥/队列已由单测覆盖。
- 三平台 Demo 实机运行和平台采集后端待后续验证。
- 设备布局一致。

---

## 6.6 6.1.5 二进制资产烘焙

### 目标

- 发行版零 JSON 解析。
- 加载更快、内存更小。

### 实现

- `AssetLoader::to_binary(const ActionAsset&)` 将内存资产写成 `.insysb`。
- `AssetLoader::load_from_binary(std::string_view)` 只读二进制，不经过 JSON 解析器。
- `AssetLoader::load_from_file()` 按扩展名分发：`.json` → JSON，`.insysb` → 二进制。
- 参考 Unity 的 `InputActionAsset.ToJson/FromJson`、`InputActionMap.WriteMapJson`
  以及 `InputActionSerializationHelpers`：保留 asset/map/action/binding 的 GUID，
  按 map 维护 action/binding 区间，并把字符串集中到字符串池。

### 格式（version 1，little-endian）

```text
AssetHeader (36 bytes):
  char     magic[8];          // "INSYAST\0"
  uint32_t version;           // 1
  uint32_t stringPoolOffset;
  uint32_t stringPoolSize;
  uint32_t mapCount;
  uint32_t actionCount;
  uint32_t bindingCount;
  uint32_t schemeCount;

StringRef assetName;         // 8 bytes，紧随 header

MapRecord[mapCount];         // 每条 40 bytes
ActionRecord[actionCount];   // 每条 60 bytes
BindingRecord[bindingCount]; // 每条 96 bytes
SchemeRecord[schemeCount];   // 每条 44 bytes
StringRef deviceRefs[...];   // 所有 scheme 的设备布局字符串
StringPool;                  // 字节串，不要求 NUL 结尾
```

- `StringRef = uint32_t offset + uint32_t length`，offset 相对 `StringPool` 起点。
- `MapRecord`：name、GUID、actionOffset/actionCount、bindingOffset/bindingCount。
- `ActionRecord`：name、GUID、type、control、processors、interactions、
  initialStateCheck、priority。
- `BindingRecord`：name、GUID、path、overridePath、action、interactions、
  processors、groups、overrideInteractions、overrideProcessors、composite 标志、
  partIndex。
- `SchemeRecord`：name、GUID、bindingGroup、deviceOffset/deviceCount、optional 标志。
- 数据区顺序固定，`stringPoolOffset` 必须位于所有记录之后。
- 校验：magic/version、计数与记录区间、字符串池边界、字符串引用边界、
  action type/composite flags 的合法值；错误统一抛出 `AssetError`。

### 工具

- CMake 目标 `asset_bake`，源码 `CppInputSystem/tools/asset_bake/`。
- 构建：`cmake --build build --config Release --target asset_bake`。
- 烘焙：`build/tools/Release/asset_bake.exe player.json player.insysb`。
- 可选 `--benchmark` 会重复加载 JSON/二进制并输出耗时对比。
- 构建期调用或手动运行均可；运行时自动识别扩展名。

验收：

- 同一资产 JSON 与二进制结构、GUID、驱动行为一致。
- `insys_tests` 覆盖完整字段往返、驱动、文件扩展名识别和损坏头/字符串引用拒绝。
- Demo 资产手工基准（Debug）：`player.json` 100 次 JSON 加载约 142.6 ms，
  `.insysb` 约 44.0 ms；二进制为 1452 bytes。耗时会随构建配置和机器变化。

---

## 6.7 6.1.6 触摸与传感器（可选）

### 已实现（核心设备）

参考 Unity `Touchscreen.cs`、`TouchControl.cs`、`Sensor.cs`：

- `Touchscreen`：10 个 touch 组（`touch0`…`touch9`），每组包含
  `position`、`delta`、`startPosition`、`phase`、`tapCount`、`press`；
  `TouchscreenState` 与布局严格对应，`touch(index)` 可取到分组。
- `TouchPhase`：`Began/Moved/Stationary/Ended/Canceled`。
- 传感器设备：`Gyroscope`、`Accelerometer`、`Gravity`、`LinearAcceleration`、
  `MagneticField`（Vector3）、`Attitude`（Quaternion）、`Light`、`Pressure`、
  `Proximity`、`Humidity`、`AmbientTemperature`、`HingeAngle`（Axis）和
  `StepCounter`（Integer），均提供状态结构、控件、工厂和默认布局。
- 基础控件：`IntegerControl`、`DeltaControl`、`DoubleControl`、
  `DiscreteButtonControl`；`IntegerControl` 支持 16/32 位状态读取，`DoubleControl`
  使用 `DBL ` 格式，`DiscreteButtonControl` 支持 min/max/wrap/null 范围判断。
- `Pen`：position/delta/tilt/pressure/twist/tip/eraser/barrel1..4/inRange/displayIndex，
  `PenState` 36 字节、格式 `PEN `。
- `TrackedDevice`：trackingState/isTracked/devicePosition/deviceRotation，
  `TrackedDeviceState` 36 字节、格式 `TRCK`。
- 新增 `Vector3Control` / `QuaternionControl`、`kFormatVector3` /
  `kFormatQuaternion` 与对应状态格式。
- 支持通过标准 state event 注入触摸/传感器数据；动作绑定可直接使用
  `<Touchscreen>/touch0/position`、`<Gyroscope>/angularVelocity` 等路径。

### Windows 触摸后端

- `WindowsTouch`（`include/insys/backend/windows_touch.h`）参考 `Touchscreen.cs` 的
  触点状态语义，使用 `RegisterTouchWindow` + `WM_TOUCH` + `GetTouchInputInfo`。
- 在 GLFW 窗口上链式安装 `WndProc`，退出时先移除触摸再恢复 Raw Input 链。
- `TOUCHINPUT` 屏幕坐标转换为 client 坐标，按 `dwID` 分配最多 10 个 touch 槽；
  映射 `Began/Moved/Stationary/Ended`，维护 `position/delta/startPosition`。
- 首次触点到达时延迟创建 `Touchscreen` 设备，并发送
  `kFormatTouchscreen` state event；没有触摸设备时不创建额外设备。
- 真实触摸屏多指/掌压/DPI 坐标验收仍待硬件；后端不可用时不影响核心库。

### 触摸后端集成核心

- `TouchBackend`（`include/insys/backend/touch_backend.h`）面向移动端/自定义触摸源：
  首次 `Began` 时延迟创建 `Touchscreen`，按外部 touch ID 分配最多 10 个槽。
- 维护 `position/delta/startPosition/phase/tapCount/press`，支持
  Began/Moved/Ended/Canceled；多次 Moved 在 `poll()` 前累加 delta。
- `Began/Moved` 接受平台压力值并钳制到 `[0,1]`；Ended/Canceled 清零。
- 同一帧所有触点合并为一个 `TouchscreenState` 事件；`poll()` 后清空 delta。
- 单测覆盖延迟创建、移动累加、结束、取消、多指、压力、10 指上限和 shutdown。
- Android JNI 与 iOS `UIGestureRecognizer` 适配已提供，应用 Activity/UIView 集成仍待完成。

### 传感器后端集成

- `SensorBackend`（`include/insys/backend/sensor_backend.h`）管理
  Gyroscope/Accelerometer/Gravity/LinearAcceleration/MagneticField/Attitude 以及
  Light/Pressure/Proximity/Humidity/AmbientTemperature/StepCounter/HingeAngle
  设备的增删和 ID 映射。
- 更新接口按传感器类型区分，类型不匹配返回失败；相同值不产生事件。
- 同一帧多次更新合并为最新状态，`poll()` 每设备最多发送一个 state event，
  避免 Manager 单帧单状态事件限制丢更新。
- 单测覆盖全部布局、值去重、类型校验、多更新合并和移除时清理脏队列。
- Android Java 侧注册见 `AndroidInput.java`。

### 移动端输入桥

- `MobileInputBridge`（`include/insys/backend/mobile_input.h`）组合
  `TouchBackend` 与 `SensorBackend`，供 Android/iOS 回调直接调用。
- 触摸支持 Began/Moved/Ended/Canceled，首次触摸延迟创建 `Touchscreen`。
- 运动传感器按 `MobileMotionSample` 的可选字段懒创建
  Gyroscope/Accelerometer/Gravity/LinearAcceleration/MagneticField/Attitude 以及
  Light/Pressure/Proximity/Humidity/AmbientTemperature/StepCounter/HingeAngle
  设备，同一帧合并后统一 `poll()`。
- 单测覆盖触摸路由、全部传感器更新、懒创建、值去重、帧合并和 shutdown。
- `MobileInputQueue` 提供互斥保护的触摸/运动事件队列，允许 JNI/sensor 线程
  `push`，主线程 `drain(bridge)` 后统一 `poll()`；单测覆盖顺序、合并、clear
  和生产者线程。队列同时透传触摸压力值，并支持 `push_suspend()` 延迟到
  主线程取消触点。
- `TouchBackend::cancel_all()` / `MobileInputBridge::suspend()` 在后台切换时
  取消所有活动触点并发送 Canceled，避免手指状态卡住。
- `android_sensor.h` 提供 `AndroidSensorType`（数值对齐 Android `Sensor.TYPE_*`
  和 Unity `AndroidSensorType`）及 `feed_android_sensor()`：
  - Accelerometer/Gyroscope/Gravity/LinearAcceleration/MagneticField 转发前三个值；
  - Light/Pressure/Proximity/RelativeHumidity/AmbientTemperature/HingeAngle
    转发第一个值；StepCounter 取整数步数；
  - RotationVector/GameRotationVector 转发前三个分量并令 `w=0`，由
    `AndroidCompensateRotation` 重建四元数；
  - 数值保持 Android 原始单位（m/s²、rad/s），需要 Unity 语义时在绑定上使用
    `AndroidCompensateDirection/Rotation`；
  - 单测覆盖六类核心传感器、环境传感器、StepCounter、GameRotationVector、
    原始单位保留和不支持类型。
- `ios_motion.h` 提供 CoreMotion 采样适配：
  - `feed_ios_gyroscope()` 对应 `CMGyroData.rotationRate`（rad/s）；
  - `feed_ios_accelerometer()` 对应 `CMAccelerometerData.acceleration`（g）；
  - `feed_ios_device_motion()` 对应 `CMDeviceMotion` 的 rotationRate、gravity、
    userAcceleration、magneticField 和 attitude（四元数直接透传）；
  - `CMDeviceMotion.userAcceleration` 是去除重力的线性加速度，映射到
    LinearAcceleration，不映射到 Accelerometer；
  - 单测覆盖独立 gyro/accel、deviceMotion 五组件和缺省组件。
- `ios_coremotion.h` / `src/backend/ios/ios_coremotion.mm` 提供实际平台回调：
  - 参考 Unity `iOSStepCounter.mm`，在 `NSOperationQueue mainQueue` 上回调，
    避免跨线程访问主线程输入状态；
  - `CMMotionManager` 同时采集 deviceMotion 与 accelerometer；deviceMotion
    不可用且提供 gyro 回调时回退到 `CMGyroData`；
  - start/stop 成对管理，错误通过 `NSLog` 输出，重复 start 返回 `false`；
  - 非 iOS 构建链接桩实现，返回不可用；当前 Windows 环境仅验证桩与接口。
- `ios_step_counter.h` / `src/backend/ios/ios_step_counter.mm` 提供 iOS 计步器适配：
  - `CMPedometer` 通过 start/stop 成对管理，计步回调切回主线程；
  - 提供可用性、授权状态和运行状态查询；非 iOS 提供不可用桩；
  - 应用仍需在 Info.plist 配置 `NSMotionUsageDescription` 并在 Main 线程
    将步数写入 `MobileInputBridge`/状态事件。
- `android_input.h` / `src/backend/android/android_input_jni.cpp` 提供 Android JNI 入口：
  - `android_input_attach()` / `android_input_detach()` 以原子指针绑定
    `MobileInputQueue`，供 Java 线程 `push`、主线程 `drain`；
  - JNI 方法对应 `java/com/insys/input/MobileInput.java`
    （`nativePushTouch` / `nativePushSensor` / `nativeSuspend`）；
  - 触摸相位使用 Unity `TouchPhase` 数值；Stationary 与未知值忽略；
  - 传感器事件复用 `make_android_motion_sample()`，经队列合并后交给
    `MobileInputBridge`；
  - `nativeSuspend` 只入队 `Suspend`，由主线程 `drain()` 调用
    `MobileInputBridge::suspend()`，避免在 Java 回调线程直接改设备状态；
  - Android 构建把核心库建为 shared，并额外生成 `insys_android_jni`
    shared 库；Java 类加载后者；应用仍需注册 `SensorEventListener` /
    处理 `MotionEvent` 并调用 `MobileInput`。
  - 当前 Windows 环境无法编译 JNI/Android 分支，仅验证可移植部分。
- `java/com/insys/input/AndroidInput.java` 提供 Java 侧平台适配：
  - `startSensors(Context)` 注册 Accelerometer/Gyroscope/Gravity/
    LinearAcceleration/MagneticField/Light/Pressure/Proximity/RelativeHumidity/
    AmbientTemperature/StepCounter/HingeAngle；
    RotationVector 不可用时回退 GameRotationVector，采样率为
    `SENSOR_DELAY_GAME`；`stopSensors()` 统一注销；
  - `onSensorChanged()` 直接把类型和 `values` 交给 `MobileInput.pushSensor`；
  - `handleMotionEvent()` 处理 DOWN/POINTER_DOWN、MOVE、POINTER_UP/UP、
    CANCEL，使用 `BitSet` 跟踪活动 pointer，压力值原样透传；
  - `suspend()` 清空本地 pointer 并调用 `MobileInput.suspend()`；
  - 应用仍需在 Activity 的 `onTouchEvent`、`onPause`、`onResume` 中调用
    对应方法；当前 Windows 环境无法编译 Java/Android 分支。
- `ios_touch.h` / `src/backend/ios/ios_touch.mm` 提供 iOS 触摸适配：
  - `feed_ios_touch()` 将 `IosTouchSample` 映射到 TouchBackend；
  - `ios_touch_attach(UIView*, callbacks)` 在任意 `UIView` 上挂载
    `UIGestureRecognizer` 子类，`cancelsTouchesInView=NO`，不阻断原触摸；
  - 使用 `UITouch*` 指针映射稳定 touch ID，坐标取 view 内点坐标（左上原点），
    压力取 `force / maximumPossibleForce`，不可用时为 1；
  - `touchesBegan/Moved/Ended/Cancelled` 分别对应 Unity TouchPhase；
  - 主线程回调；`ios_touch_detach()` 移除 recognizer；非 iOS 提供桩实现。
  - 当前 Windows 环境无法编译 `.mm`，需 iOS 工具链与实机验证。
- 应用 Activity/UIView 集成、实机验证仍待完成。

### 待做（其他平台）

- Linux `evdev`/libinput、macOS `GameController.framework` 传感器/手柄采集。
- 应用 Activity/UIView 集成、实机验证。
- 真实硬件验收：多指触摸、设备方向、传感器噪声/校准。

优先级低，按项目需求决定。

---

## 6.8 6.1.7 网络远程输入（可选）

### 已实现（核心）

参考 Unity `InputRemoting.cs` / `RemoteInputPlayerConnection.cs`：

- `InputRemoting` 支持双向设备镜像：发送端注册本地设备、初始状态和后续
  state/reset 事件；接收端按 `participantId + remoteDeviceId` 建立本地设备映射。
- 消息类型对齐 Unity 的核心子集：`Connect`、`Disconnect`、`NewDevice`、
  `NewEvents`、`RemoveDevice`、`StartSending`、`StopSending`。
- 自定义二进制帧：magic `INSYREM1`、version、消息类型、participantId、负载长度；
  字符串和事件负载均做长度/截断校验。
- 远程设备设置 `Device::remote = true`，不会再次转发，避免回环；`NewDevice`
  携带 layout、名称、设备描述和 state format，接收端校验格式后创建。
- `LoopbackTransport` 用于同进程双向测试；`TcpServer` / `TcpTransport` 提供
  IPv4 TCP 传输（Windows Winsock / POSIX sockets，非阻塞读写，消息按
  `INSYREM1` 帧切分）。`UdpTransport` 提供 IPv4 UDP best-effort 传输，
  单包上限 65507 bytes；UDP 不保证顺序/可靠，设备创建消息可能丢失，需要可靠
  通道时使用 TCP。`WebSocketTransport` 实现 RFC6455 客户端/服务端握手和 binary
  frame（SHA-1 + Base64 accept key，支持 ping/pong、fragmentation 和 close）；
  当前仅明文 ws，无 TLS，路径由 `connect`/`accept` 的 path 校验。
- 当前只支持接收端已注册的 layout；动态布局（Unity `NewLayout`）和跨版本协议
  迁移未实现。接收端每个设备每个 update step 只应用一次完整状态事件，发送端
  应避免同一帧内对同一设备连发多个完整状态。

示例：

```cpp
auto pair = LoopbackTransport::create_pair();
InputRemoting sender(senderManager, pair.first);
InputRemoting receiver(receiverManager, pair.second);
sender.start_sending();
receiver.update();          // 创建远程设备并排入初始状态
receiverManager.update();   // 应用初始状态
```

TCP 示例：

```cpp
TcpServer server;
server.listen("127.0.0.1", 0);
auto client = TcpTransport::connect("127.0.0.1", server.port());
auto accepted = server.accept();
InputRemoting sender(senderManager, *client);
InputRemoting receiver(receiverManager, *accepted);
```

UDP 示例：

```cpp
auto first = UdpTransport::bind("127.0.0.1", 0);
auto second = UdpTransport::bind("127.0.0.1", 0);
first->set_peer("127.0.0.1", second->local_port());
second->set_peer("127.0.0.1", first->local_port());
```

WebSocket 示例：

```cpp
TcpServer server;
server.listen("127.0.0.1", 0);
auto client = WebSocketTransport::connect("127.0.0.1", server.port(), "/ws");
auto accepted = WebSocketTransport::accept(server, "/ws");
InputRemoting sender(senderManager, *client);
InputRemoting receiver(receiverManager, *accepted);
```

### 验收

- `insys_tests` 覆盖消息往返、坏 magic/截断/未知类型拒绝、设备镜像、初始状态、
  按键状态转发、设备移除、reset 转发、握手触发发送和断连清理。
- TCP 测试在 localhost 上覆盖双向消息和完整设备状态镜像；UDP 测试覆盖双向
  datagram、peer 学习和超大数据拒绝；WebSocket 测试覆盖 RFC accept key、握手和
  双向 binary frame。真实网络延迟/丢包/断线重连仍需长稳测试。

---

## 6.9 验收标准（按选项）

每个选项独立验收：

- Raw Input：原始 delta 正确、多鼠标可用。
- XInput：震动可用、无重复设备。
- 跨平台：Linux evdev 映射/聚合/事件解码/能力位分类/采集会话/热插拔差异/后端集成、Linux 平台源/inotify watcher、macOS GameController 映射/后端、传感器后端、触摸后端和移动端桥/队列单测通过；三平台 Demo 实机运行待验证。
- 二进制资产：行为一致、加载更快。
- 触摸/传感器：Windows 触摸后端编译/启动验证，真实多指硬件待验收；其他平台待做。
- 远程输入：Loopback/TCP/WebSocket 下设备映射和事件顺序正确，UDP 支持 best-effort datagram；
  真实网络延迟/丢包/断线重连待长稳验证。

---

## 6.10 风险与建议

1. **平台差异**：手柄映射在各平台不一致，建议以 XInput/GameController 为准，GLFW 作为回退。
2. **Raw Input 与 GLFW 冲突**：两者都处理鼠标时要去重（Raw Input 只负责 delta，GLFW 负责位置/按钮）。
3. **震动命令**：需要设计设备命令协议，避免把平台 API 泄漏到核心库。
4. **二进制格式**：一旦发布需保持向后兼容，建议带 version + 迁移逻辑。
5. **优先级**：M6 全部可选，优先保证 M0–M5 的稳定与验收。

---

## 6.10.1 实现记录（M6 Windows 平台子集）

已实现 6.1.1、6.1.2、6.1.3 核心/死区曲线/基础手势/平滑、6.1.5、6.1.6 核心设备/
Windows 触摸和 6.1.7 远程输入核心/TCP/UDP/WebSocket 传输，参考 Unity 的
`Mouse.cs`、`XInputController.cs`、`XInputControllerWindows.cs`、`XInputSupport.cs`、
`Gamepad.cs`、`DualMotorRumbleCommand.cs`、`Touchscreen.cs`、`TouchControl.cs`、
`InputActionAsset.cs`、`InputActionMap.cs`、`InputActionSerializationHelpers.cs`、
`InputRemoting.cs` 和 `RemoteInputPlayerConnection.cs`。

### Raw Input

- `Settings::useRawInput` 控制是否启用；Demo 使用 `--raw-input`。
- `WindowsRawInput` 在 GLFW 窗口上链式安装 `WndProc`：
  - 注册 `RIDEV_INPUTSINK | RIDEV_DEVNOTIFY`。
  - 处理 `WM_INPUT` 的 `RIM_TYPEMOUSE` 相对 delta。
  - 忽略 `MOUSE_MOVE_ABSOLUTE`，避免远程桌面/绝对设备产生伪 delta。
  - 处理 `WM_INPUT_DEVICE_CHANGE` 的 `GIDC_REMOVAL`。
- 原始 delta 在 `GlfwBackend::poll()` 中按设备合并为一帧一次事件，避免高轮询率下事件爆炸。
- 第一个 Raw Input 鼠标复用 GLFW 的 `Mouse` 设备（位置/按钮仍来自 GLFW）；
  后续物理鼠标创建 `interfaceName = "RawInput"` 的独立 `Mouse` 设备，各自更新 `delta`。
- GLFW 光标回调在 Raw Input 模式下只更新位置和按钮，不再用系统光标差值覆盖原始 delta。
- 退出时恢复原 `WndProc`、注销 Raw Input，并移除额外的 Raw Input 鼠标设备。

### Windows 触摸

- `WindowsTouch` 链式安装 `WndProc`，处理 `WM_TOUCH`，用 `GetTouchInputInfo`
  读取 `TOUCHINPUT` 并按 `dwID` 映射到最多 10 个槽。
- 屏幕坐标转 client 坐标；`Began/Moved/Stationary/Ended` 和
  `position/delta/startPosition/press` 写入 `TouchscreenState`。
- 首次触摸时创建 `Touchscreen` 设备并发送 `kFormatTouchscreen` state event；
  后端初始化失败时不创建设备。退出时先卸载触摸 WndProc，再恢复 Raw Input 链。
- 真实触摸屏多指/掌压/DPI 验收待硬件。

### Linux evdev 映射

- `include/insys/backend/evdev_mapping.h` 提供平台无关的 KEY/REL/ABS/BTN 映射。
- `MouseMapper` 和 `GamepadMapper` 已接入单测，覆盖滚轮、侧键、摇杆/扳机归一化、
  dpad 和常用手柄按钮。
- `EvdevDeviceState` 聚合键盘位图、鼠标和手柄状态，维护 frame dirty 标记，
  单测覆盖帧边界和 reset。
- `EvdevEventDecoder` 解码 `input_event` 原始记录（默认 24 字节/时间戳 16 字节，
  可配置 32 位布局），跨多次 `feed` 缓存不完整记录；`decode_evdev_capabilities`
  解析 type/key/rel/abs 能力位，`classify_evdev_device` 据此分类键盘/鼠标/手柄。
- `EvdevCaptureSession` 管理多设备 decoder/state，轮询注入式读取函数，
  `SYN_REPORT` 后返回去重的脏设备路径；单测覆盖半包、分块读取、多设备和移除。
- `EvdevDeviceTracker` 对扫描快照做路径/类型差异比较，输出 Added/Removed 并按
  类型变化产生替换；单测覆盖空路径、重复路径和 clear。
- `EvdevBackend` 将扫描、热插拔、采集会话和 `Manager` 串起来：创建
  Keyboard/Mouse/Gamepad、编码键盘/鼠标/手柄状态事件、鼠标 delta 发送后清零；
  单测覆盖三设备状态、移除、无 source 重试和类型变化替换。
- `linux_evdev` 扫描 `/dev/input/event*`，用 `EVIOCGBIT` 查询能力并分类；source
  打开非阻塞 fd，将 `read()` 的 `input_event` 归一化为 24 字节记录；非 Linux
  平台提供空实现。
- `InotifyEventDecoder` 解析 `inotify_event` 字节流，处理 name 填充和半包，
  `is_input_scan_event` 过滤设备增加/删除/移动等触发。
- `InputHotplugMonitor` 只接受 `event*` 名称或 watch 级事件；
  `LinuxInotifyWatcher` 对 `/dev/input` 建立非阻塞 watch 并读取事件。
- Linux 分支尚未在实机编译/运行，watch 丢失后的自动重建未实现。

### macOS GameController 映射/后端

- `include/insys/backend/gamecontroller_mapping.h` 定义 `GameControllerState` 和
  `map_gamecontroller_state`。
- 映射 A/B/X/Y、shoulder、trigger、左右摇杆、dpad、menu/options、摇杆按键，
  轴和扳机钳制到合法范围。
- `GameControllerBackend` 管理控制器 ID、创建/移除 `Gamepad`、合并多次更新并
  在 `poll()` 发送最新状态；单测覆盖映射、去重、多控制器和移除。
- `GameController.framework` 设备发现/回调和 IOHIDManager 原始鼠标 delta 未实现。

### XInput

- `XInputBackend` 轮询 4 个 XInput slot，热插拔创建/移除 `Gamepad` 设备。
- `xinput_mapping.h` 将 XInput 按钮/摇杆/扳机映射为现有 `GamepadState`：
  - A/B/X/Y → South/East/West/North；
  - 保留 dpad、start/back、L3/R3、LB/RB；
  - 摇杆使用 `[-1, 1]` 归一化，Y 保持 XInput 的正向上约定。
- `GlfwBackend::set_skip_xinput_gamepads(true)` 会跳过名称含 `Xbox`/`X-Input`/`XInput`
  的 GLFW 手柄，避免与 XInput 设备重复；Demo 使用 `--xinput`。
- 震动命令：
  - `include/insys/device_commands.h` 定义
    `GetHapticCapabilitiesCommand`、`SendHapticImpulseCommand`、
    `SendBufferedHapticsCommand`。
  - `Runtime` 支持可替换的 `DeviceCommandHandler`。
  - `Manager::send_device_command` / `System::send_device_command` 转发命令。
  - XInput 支持低频/高频双马达；`SendHapticImpulseCommand::duration` 对 XInput
    忽略，由调用方发送 stop 命令；扳机马达和 buffered haptics 返回不支持。

### 二进制资产

- `AssetLoader::to_binary` / `load_from_binary` 使用固定小端记录和去重字符串池。
- `load_from_file` 按 `.json` / `.insysb` 扩展名选择解析器，二进制路径不调用 JSON parser。
- `tools/asset_bake` 支持烘焙和 `--benchmark`；unit test 覆盖结构往返、GUID、驱动、
  大小写扩展名、magic/version/pool/string-ref/truncation 校验。
- 实测 `player.json` 二进制 1452 bytes；Debug 100 次加载 JSON 约 142.6 ms、
  二进制约 44.0 ms。

### 远程输入

- `InputRemoting` 参考 `InputRemoting.cs` 的 participant/device 映射和消息类型，
  使用自定义二进制帧而不是 Unity 的 JSON payload。
- `NewDevice` 发送 layout、名称、描述和 state format；接收端通过 `DeviceBuilder`
  重建设备并标记 `Device::remote`，后续 `NewEvents` 只做 device ID 重映射。
- `LoopbackTransport` 提供双向内存队列，单测不需要端口或线程；
  `TcpServer` / `TcpTransport` 提供 IPv4 TCP 传输（Winsock/POSIX，非阻塞）；
  `WebSocketTransport` 提供 RFC6455 binary WebSocket；
  `UdpTransport` 提供 best-effort IPv4 datagram。
- 远程设备事件不会被再次转发；`Disconnect` 会移除该 participant 的设备并停止发送。

### 验证

- 单测：`insys_tests` 287 用例 / 3493 断言，包含 XInput 映射、名称识别、设备命令转发、
  CompensateDirection/Rotation、修饰键合成器、平滑、手势、evdev 映射/状态聚合/事件解码/能力位分类/采集会话/热插拔差异/后端集成、Linux 平台源/inotify watcher、GameController 映射/后端、传感器后端、触摸后端、移动端桥/队列、`.insysb` 完整字段往返、
  驱动、扩展名识别和损坏数据拒绝，以及远程消息/设备镜像/状态转发。
- Windows 触摸后端编译通过，Demo smoke 正常启动/关闭；真实多点触摸硬件验收待补。
- Demo `--raw-input` 使用 `SendInput`/`mouse_event` 注入，Raw Input 成功收到相对 delta；
  `--xinput` 在无手柄环境可正常启动和关闭。
- 未完成的人工硬件验收：多只物理鼠标区分、真实 XInput 手柄按钮/扳机、震动强度。
- 6.1.3 核心项、死区曲线、基础手势、平滑、6.1.5 二进制烘焙、6.1.6 核心设备/
  Windows 触摸/传感器后端/触摸后端/移动端桥/事件队列和 6.1.7 远程输入核心/TCP/UDP/WebSocket 传输已完成；
  Linux evdev 映射/聚合/事件解码/能力位分类/采集会话/热插拔差异/后端集成/
  `/dev/input` 平台源和 macOS GameController 映射/后端已实现；Linux 实机验证和
  macOS framework 采集未完成。

## 6.11 完成

M6 是可选增强清单；当前已完成 6.1.1 Win32 Raw Input、6.1.2 XInput、
6.1.3 的核心处理器/合成器、死区曲线、基础手势、平滑、6.1.4 Linux evdev 映射/聚合/事件解码/能力位分类/采集会话/热插拔差异/后端集成与 macOS GameController 映射/后端、
6.1.5 二进制资产烘焙、6.1.6 核心设备/Windows 触摸/传感器后端/触摸后端/移动端桥/事件队列和
6.1.7 远程输入核心/TCP/UDP/WebSocket 传输、6.1.8 EnhancedTouch/OnScreen，
其余选项按项目需要继续实现。
核心与 M0-M5 主线保持稳定。
如后续需要，可在本目录新增：

- `M8-Editor.md`：独立输入资产编辑器（Web 前端 + C++ 服务）。
- `M9-Networking.md`：远程输入/云游戏。
- `M10-XR.md`：XR 设备支持。

---

## 6.12 高层玩家 API（InputUser / PlayerInput）

已实现与 Unity `InputUser.cs`、`PlayerInput.cs` 对齐的高层玩家接入层：

- `InputUser`：
  - 静态 `join` / `leave` / `all` / `find` 管理玩家生命周期。
  - 加入时复制 `ActionAsset`，每个玩家拥有独立的 ActionState、处理器和回调。
  - 设备配对 `pair_device` / `unpair_device` / `pair_devices`，并按玩家过滤
    `ActionAsset::devices`，避免多个玩家读取同一设备。
  - 从 `ActionAsset::controlSchemes` 自动匹配设备布局；非 optional 方案优先，
    再按匹配设备数选择。
  - 自动同步控制方案的 binding group 到 `ActionAsset::bindingMask`。
  - `enable_action_map` / `disable_action_map` / `switch_action_map` 管理每个
    玩家自己的 ActionMap。
- `PlayerInput`：
  - `PlayerInput::join` 便捷入口，默认只激活 `defaultActionMap`。
  - `PlayerInput::all`、`player_index` 和 `switch_current_action_map`。
- 设备热插拔：
  - 玩家监听 `DeviceChange`；新设备可自动补齐缺失方案，移除设备会清理配对并
    重新解析绑定。
- `BindingResolver` / `ActionState` 已支持按设备列表和 binding mask 解析；重新
  绑定（rebinding）时保留玩家自己的设备过滤。

```cpp
auto* player = insys::PlayerInput::join(manager, templateAsset, "Gameplay");
player->switch_current_action_map("UI");
insys::InputUser::leave(player);
```

验证：`insys_tests` 覆盖方案匹配、玩家隔离、设备配对/解绑、设备移除、binding
mask、ActionMap 切换和加入/离开；当前 287 用例 / 3493 断言通过。

限制：控制方案按设备 layout 精确匹配，尚未实现 Unity 的 layout 继承距离、OR
设备需求和 `PlayerInput` 的 NotificationBehavior/UnityEvent 层；没有
`controlSchemes` 的资产不会自动配对，需要显式调用 `pair_device` /
`pair_devices`。真实设备插拔与多玩家游戏接入仍需应用层验收。

## 6.13 EnhancedTouch 与 OnScreen

参考 Unity `EnhancedTouch`、`TouchSimulation`、`OnScreenControl`、
`OnScreenButton`、`OnScreenStick` 和 `InputSystemUIInputModule` 实现。

### EnhancedTouch

```cpp
insys::enhanced_touch::EnhancedTouchSupport::enable(manager);
manager.update();
for (const auto& touch : insys::enhanced_touch::Touch::active_touches())
    use(touch.screen_position(), touch.phase());
insys::enhanced_touch::EnhancedTouchSupport::disable();
```

- `EnhancedTouchSupport::enable(manager)` / `disable()` 管理全局支持，
  支持引用计数；`Manager` 销毁时自动清理。
- 每帧在 `Manager::update()` 后查询 `Touch::active_touches()`，或显式调用
  `EnhancedTouchSupport::update()`。
- `Touch` 提供 phase、touch id、screen position、start position、delta、
  pressure、tap、start/time、finger/screen 和 history。
- `Finger` 对应 `Touchscreen` 的 `touch0`…`touch9`，提供 `last_touch`、
  `current_touch`、`is_active` 和完整 `TouchHistory`；`Touch::history()`
  返回当前触点之前的记录（不包含当前记录）。
- `on_finger_down` / `on_finger_move` / `on_finger_up` 以回调 id 注册和移除。
- `max_history_length_per_finger` 默认 64，按触点记录裁剪；提供
  `Touch::max_history_length_per_finger()` 只读入口。
- `TouchSimulation` 从 `Mouse` 左键和 `Pen` tip 生成 “Simulated Touchscreen”
  的 Began/Moved/Ended 状态事件，可显式 `add_pointer` 接入其他 Pointer。
  平台触摸 ID、radius、display index 和 primary touch 尚未进入
  `TouchPointState`，因此 EnhancedTouch id 为本地合成值。

### OnScreen 与通用 UI 指针输入

```cpp
insys::onscreen::OnScreenInputModule input(manager);
insys::onscreen::OnScreenStick stick;
stick.set_control_path("<Gamepad>/leftStick");
stick.set_bounds({0.0f, 0.0f, 200.0f, 200.0f});
input.add_control(&stick);
// 每帧：input.update(); manager.update();
```

- `ui::UiPointerInputModule` 是通用指针分发层：支持鼠标和最多 10 个触摸点，
  按下时按目标添加顺序反向命中 `UiRect`，捕获后持续发送 move/up。
- `onscreen::OnScreenInputModule` 在 `UiPointerInputModule` 上按设备 layout
  懒创建虚拟设备（如 `Gamepad`），同 layout 的多个控件共享设备；控件值先
  写入组状态，`update()`/`flush()` 合并为一条 full state event。
- `OnScreenButton`：按下写 1，抬起/复位写 0。
- `OnScreenStick`：支持 `RelativePositionWithStaticOrigin`、
  `ExactPositionWithStaticOrigin`、`ExactPositionWithDynamicOrigin`，按
  `movement_range` 归一化并钳制；动态原点受 `dynamic_origin_range` 限制。
- 控件使用屏幕空间 `UiRect` 作为命中区域，不依赖具体 UI 框架；任意 UI 元素
  可实现 `UiPointerTarget` 接入同一模块。
- 设备热插拔重建状态缓冲时保留已有设备状态，避免创建 OnScreen 设备清空
  当前鼠标/触摸状态。

限制：EnhancedTouch 的全局支持同一时间绑定一个 `Manager`；OnScreen 控件和
模块不拥有彼此的生命周期。未实现 UGUI Canvas/RectTransform 或 RmlUi 元素
绑定，控件位置需由应用同步；未实现触摸压力驱动、视觉位移、触摸 ID 复用、
多击间隔和 primary touch 语义。当前 Windows 环境仅通过合成事件和单元测试
验证，移动端实机 UI 仍需应用层接入与验收。
























































































