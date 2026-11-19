# M7 平台外设生态：HID 与控制器家族

参考 Unity Input System 的 `HIDParser`、`HID` 布局构建、`DualShock`、
`Switch`、`Steam` 和 haptics 结构实现。平台无关的报告层提供通用解析、
产品布局和 `HidBackend`；Windows/Linux/macOS 平台源通过 SetupAPI/HID、
hidraw 与 IOHIDManager 完成真实设备枚举、读取和写入。

## HID 通用解析

公共接口在 `include/insys/hid/hid_descriptor.h`：

- `parse_report_descriptor(data, size, descriptor)` 解析 HID Item 流，
  生成 `Element`、`Collection` 和 `ReportInfo`。
- 支持 Usage Page、Usage/Usage Minimum/Maximum、Logical/Physical Min/Max、
  Report ID/Size/Count、Input/Output/Feature、Collection/End Collection、
  Push/Pop 和 long item 跳过。
- `Element` 保留 usage page、usage、报告类型、report id、bit offset、
  bit size、flags、logical/physical 范围。
- `ReportInfo` 汇总每种 report id + 类型的 bit size；顶层 application
  collection 的 usage/usagePage 写入 `DeviceDescriptor`。
- 无 report id 的设备使用 id=1、`hasId=false`，报告内偏移从 0 开始；
  有 report id 时偏移从 8 bit 开始。

`include/insys/hid/hid_layout.h` 的 `build_generic_layout` 把解析结果转成
运行时布局：

- Generic Desktop X/Y/Z/Rx/Ry/Rz/Vx/Vy/Vz/Slider/Dial/Wheel 生成 Axis。
- Button page 生成 `trigger`（usage 1）或 `buttonN`。
- 8 位/16 位轴的 logical range 被写入 `AxisControl` 的归一化参数；
  摇杆第二轴默认反向，unsigned 语义扳机以 logical minimum 作为 0 点，
  输出 0..1。
- Hat Switch 生成 `hat/up/right/down/left` 的 `DiscreteButton` 子控件。
- Generic Desktop Gamepad/Joystick 自动生成语义控件：`leftStick`/`rightStick`
  （或 `stick`）、`leftTrigger`/`rightTrigger`、`dpad` 和标准 face/shoulder/
  start/select 按钮；原始 `x/y/z/buttonN` 仍保留。
- 摇杆/扳机语义使用描述符顺序和 report offset：当 X/Y/Z/Rz 与 Rx/Ry 同时
  存在时，优先把位于首个 Button 之前的一对作为右摇杆，位于 Button 之后的
  一对作为左右扳机；这样 DS4 风格的 `X,Y,Z,Rz ... Rx,Ry` 描述符不会把
  扳机误识别为右摇杆。没有 Rx/Ry 时，signed Z/Rz 作为右摇杆，unsigned
  Z/Rz 作为左右扳机；Slider 作为 leftTrigger fallback。
- 布局状态格式为 `HID `，状态字节是原始 input report（包括 report id，
  如果描述符使用了 report id）。多个 input report id 合并到同一个 Device
  的连续状态区间：每个 report 保留自己的 `stateOffset`，布局控件偏移加上
  该区间基址，避免同一控制器的按钮、触发器和 IMU 被拆成多个 Device。
- `HidBackend` 根据报告首字节动态选择 report id；如果平台已经剥掉 report id，
  则优先按 payload 大小匹配 report。一次 `poll()` 最多为该 Device 产生一条
  完整状态事件。

## 控制器家族

已在 `default_layouts` 注册以下布局，均以 `Gamepad` 状态前 28 字节为基座，
后面追加额外按钮、左右 touchpad、gyro/accel：

| 布局 | Unity 对应 | 主要附加控件 |
|---|---|---|
| `HidGamepad` | HID 基座 | `leftTriggerButton`、`rightTriggerButton`、`systemButton`、`touchpadButton`、`micButton`、`capture`、`home`、grip/back、左右 touchpad |
| `DualShock3` / `DualShock3GamepadHID` | `DualShock3GamepadHID` | system、左右 trigger button、双马达震动；无 touchpad/mic |
| `DualShock4` / `DualShock4GamepadHID` | `DualShock4GamepadHID` | system、touchpad、左右 trigger button、lightbar |
| `DualSense` / `DualSenseGamepadHID` | `DualSenseGamepadHID` | 在 DS4 基础上增加 `micButton` |
| `SwitchProController` / `SwitchProControllerHID` | `SwitchProControllerHID` | `capture`、`home`，ZL/ZR 映射到数字 trigger |
| `SteamController` | Steam Controller | 左右 touchpad、grip/back；Steam 报告为 best-effort HID 解码 |
| `SteamDeckController` / `SteamDeckControllerHID` | Steam Deck HID | 双摇杆、双扳机、触控板、grip/back、IMU；Steam Input API 仍为外部依赖 |

`HidGamepadState` 固定为 72 字节：`GamepadState`（28）+ extraButtons（4）
+ leftTouchpad（8）+ rightTouchpad（8）+ angularVelocity（12）+
acceleration（12）。

报告解码：

- `decode_dualshock3`：32 字节 HID payload（可选带 report id），按 Unity
  `DualShock3HIDInputReport` 的 offset 解码按钮、摇杆和扳机；输出支持
  49 字节 report 0x01 的双马达震动。
- `decode_dualshock4`：USB report id 0x01，Bluetooth 0x11–0x19（带
  `data[1] & 0x80` header）；支持 9 字节裸报告。full report 解码
  gyro/accel（1024/deg/s、8192/g）和 12-bit touch point（1920×942）。
  Bluetooth 78 字节 full report 使用 seed 0xA1 校验末尾 CRC32，失败则丢弃。
- `decode_dualsense`：USB 0x01 full/minimal，Bluetooth 0x31；支持 DS4
  兼容的按钮位。full report 解码 gyro/accel 和 touch point（1920×1080），
  touch contact 写入 `extraButtons`。Bluetooth 78 字节 report 使用
  seed 0xA1 校验输入 CRC32。
- `decode_switch_pro`：0x3F simple、0x30 full、官方 accessory 的
  7/8/9 字节 input-only 报告；0x30 full report 解码最后一帧 IMU
  （±8g、±2000dps 默认量程）。
- `decode_steam_controller`：报告 0x01 的按钮、trigger、左右 touchpad
  通道；Steam Input API 仍通过外部 Steamworks 集成。
- `decode_steam_deck`：Steam Deck 64 字节 report（header 0x01 0x00 0x09），
  解码双摇杆、双扳机、触控板、grip/back、dpad、按钮和 IMU。

## Haptics

`include/insys/device_commands.h` 的命令由 `HidBackend` 路由：

- `GetHapticCapabilitiesCommand`：双马达能力；DS3/DS4/DualSense/Switch/
  Steam Controller/Steam Deck 暴露低/高马达能力；generic HID 通过
  `maxBufferSize` 暴露原始 output report 能力。
- `SendHapticImpulseCommand`：低/高马达幅度；保留每个 slot 的当前幅度。
- `SendBufferedHapticsCommand`：直接把原始报告交给平台 output 写入回调。
- `SetLightBarColorCommand`：DS4/DualSense lightbar RGB；与 rumble 合并到同一个
  output report，避免 DualSense 单独设置颜色时清掉震动。
- `SetAdaptiveTriggerCommand`：DualSense 左右 trigger 的原始 mode + 10 参数
  或 `AdaptiveTriggerEffect::feedback/weapon/vibration/bow` 预设，与
  rumble/lightbar 合并编码；`HapticCapabilities` 暴露左右 trigger 能力。
- `SendSwitchSubcommandCommand`：发送任意 Switch 0x01 subcommand report。
- `SendHidOutputReportCommand` / `SendHidFeatureReportCommand`：发送任意
  raw HID output/feature report；`SendHidReportCommand::prependReportId`
  控制是否把 `reportId` 动态插到 payload 前，适用于多 report ID 设备。
- `GetHidFeatureReportCommand`：同步读取 feature report；调用方提供
  report id、buffer 和 capacity，返回实际字节数并写回 `reportSize`。

编码器在 `include/insys/hid/hid_gamepad.h`：

- DualShock 3：output report 0x01，49 字节，小马达开关在 byte 2，
  大马达幅度在 byte 4。
- DualShock 4 USB：output report 0x05，32 字节，高/低马达在 byte 4/5，
  lightbar RGB 在 byte 6/7/8（flags bit 0x02）。
- DualShock 4 Bluetooth：report 0x11，78 字节，hw_control 0xC0，
  common payload 从 byte 3 开始，末尾 4 字节 CRC32（seed 0xA2）。
- DualSense USB：output report 0x02，48 字节，payload flags 在 byte 1/2，
  高/低马达在 byte 3/4，lightbar RGB 在 byte 45/46/47；左/右 adaptive
  trigger effect 在 byte 22/11。
- DualSense Bluetooth：report 0x31，78 字节，sequence/tag 在 byte 1/2，
  common payload 从 byte 3 开始，末尾 CRC32。
- Switch Pro：rumble-only report 0x10，9 字节，左右马达各 4 字节。
- Steam Controller：best-effort 0x8F 64 字节报告。
- Steam Deck：65 字节 feature report（report id 0x00，消息 0xEB），
  `HidBackend` 通过平台 feature report 写入回调发送。
- `hid_crc32` 实现 Linux `hid-playstation` 相同的 seed + reflected CRC32；
  输入 CRC 使用 seed 0xA1，输出 CRC 使用 seed 0xA2。

## Switch Pro handshake

`encode_switch_magic_output` 生成 Unity `SwitchMagicOutputReport` 对应的
report type 0x80 + command id 报告：Bluetooth 形态 49 字节，USB 形态 64 字节。
`HidBackend` 在检测到 Switch Pro 后按 Unity 顺序发送：

- `Status` → `Handshake` → `Highspeed` → `Handshake` → `ForceUSB`
- 完成后依次发送 `SetInputReportMode(0x30)`、`EnableImu(0x01)` 和
  `EnableVibration(0x01)` subcommand。
- 每步间隔 100 ms；先尝试 Bluetooth 49 字节报告，失败后回退 USB 64 字节报告。
- `encode_switch_subcommand` 支持 0x01 output report、packet counter、8 字节
  rumble 和任意 subcommand payload；`SendSwitchSubcommandCommand` 允许应用
  直接发送完整 subcommand。`SwitchSubcommand` 覆盖配对、SPI Flash
  读写/擦除、NFC/IR、GPIO、player lights、HOME light、IMU 灵敏度和
  电压查询；`make_switch_player_lights_payload`、
  `make_switch_home_light_payload`、`make_switch_imu_sensitivity_payload`、
  `make_switch_spi_read/write/erase_payload` 和 NFC/IR payload helper
  生成对应参数。
- `HidBackend::poll(double)` 暴露可注入时间，便于单测 handshake 定时。

`SetAdaptiveTriggerCommand` 在 DualSense 上保存左右 trigger effect
（mode + 10 参数或 Feedback/Weapon/Vibration/Bow 预设），并与
rumble/lightbar 合并到同一 output report。Steam Input API 需要 Steamworks
SDK，未内置；Steam Deck 已支持 HID 报告解码和 feature-report 震动。

## Windows/Linux/macOS HID 平台源

`include/insys/backend/windows_hid.h` / `src/backend/windows/windows_hid.cpp`
在 Windows 上把系统 HID 设备接入 `HidBackend`：

- 使用 SetupAPI 枚举 HID device interface，按 Generic Desktop usage
  0x01/Joystick(0x04)、Gamepad(0x05)、MultiAxis(0x08) 过滤，避免把键盘、
  鼠标等无关 HID 创建成输入设备。
- 通过 `HidD_GetPreparsedData`/`HidP_GetCaps` 获取 input/output/feature
  report 长度和 top-level usage；通过 `HidD_GetAttributes` 获取 VID/PID，
  通过 HID string API 获取 manufacturer/product/serial；通过
  `IOCTL_HID_GET_REPORT_DESCRIPTOR` 读取原始报告描述符。
- 每个设备一个 overlapped `ReadFile` 读取线程，输入报告进入线程安全的
  `HidReportQueue`；主线程在 `poll()` 中 `drain()` 后再调用
  `HidBackend::poll()`，保证设备状态只在主线程更新。
- 每 500 ms 重新枚举一次热插拔；读取线程因拔出/错误退出后，主线程清理
  slot 并调用 `HidBackend::remove_device`。移除时先 join reader，再丢弃该
  id 的待处理报告，避免拔出后同路径重连时旧报告落到新 slot。
- 输出报告通过 `HidD_SetOutputReport`、feature report 通过 `HidD_SetFeature`
  写回设备，失败时在平台层重试；Steam Deck 的 65 字节 feature report 也走
  同一条路径。feature report 读取使用 `HidD_GetFeature`。
- 使用 `SPDRP_ENUMERATOR_NAME` 和 device path 识别 USB/Bluetooth transport；
  SetupAPI 枚举结果按 path 去重，`HidBackend` 再用 VID/PID/usage/serial
  拒绝同物理设备的重复接口。打开/读取/写入失败会通过 `HidError` 上报。
- 输入队列有容量上限（默认 4096）；满时丢弃最旧报告并上报
  `ReportQueueOverflow`，主线程每帧排空，避免慢帧无限占用内存。
- `Settings::useHid` 打开平台源；`GlfwBackend` 在启用时跳过 GLFW gamepad
  轮询，避免同一个控制器出现重复设备。Demo 使用 `--hid` 启动。

Linux 对应实现在 `include/insys/backend/linux_hid.h` /
`src/backend/linux/linux_hid.cpp`：

- 扫描 `/sys/class/hidraw`，打开 `/dev/hidrawN`，通过 `HIDIOCGRAWINFO`
  获取 VID/PID，通过 `HIDIOCGRDESCSIZE`/`HIDIOCGRDESC` 读取描述符，再
  用 `parse_report_descriptor` 过滤 Generic Desktop
  Joystick/Gamepad/MultiAxis。
- 每个设备一个非阻塞 `poll`/`read` 线程，报告进入同一个
  `HidReportQueue`；队列上限和溢出上报与 Windows 相同。
- 每 500 ms 重新扫描热插拔；输出通过 `write()` 写回 hidraw，处理 partial
  write/EINTR/EAGAIN；feature report 通过 `HIDIOCSFEATURE` 写入，
  `HIDIOCGFEATURE` 读取。
- `HIDIOCGRAWINFO.bustype` 识别 `BUS_USB`/`BUS_BLUETOOTH` transport；
  打开失败为 `EACCES`/`EPERM` 时通过 `HidError` 提示 udev 权限。
- `Settings::useHid` 和 Demo `--hid` 在 Linux 上同样生效。

macOS 对应实现在 `include/insys/backend/macos_hid.h` /
`src/backend/macos/macos_hid.cpp`：

- 使用 `IOHIDManager` 按 Generic Desktop Joystick/Gamepad/MultiAxis
  matching dictionary 枚举设备，读取 VID/PID、usage、字符串属性和
  `kIOHIDReportDescriptorKey` 原始描述符。
- 每个设备注册 `IOHIDDeviceRegisterInputReportCallback`，输入报告进入
  `HidReportQueue`；`poll()` 驱动 main run loop 并排空队列。
- 每 500 ms 刷新设备集合；输出/feature report 分别通过
  `IOHIDDeviceSetReport(kIOHIDReportTypeOutput/Feature)` 写入，
  `IOHIDDeviceGetReport(kIOHIDReportTypeFeature)` 读取；写入失败在平台层重试。
- `kIOHIDTransportKey` 映射 USB/Bluetooth/Other；设备集合按 IORegistry
  entry id 去重，移除时丢弃该 id 的待处理报告。
- 当前源码未在 macOS 实机/CI 编译验证。

`HidReportQueue` 在 `include/insys/backend/hid_report_queue.h`，只依赖
`HidBackend`，可在非 Windows 平台单测；它的 `drain()` 返回实际接受的
报告/移除事件数，`take_dropped_count()` 返回上一轮因容量上限丢弃的报告数。

## HidBackend 接入

`include/insys/backend/hid_backend.h` 提供平台无关的报告入口：

```cpp
insys::backend::HidBackend hid;
hid.initialize(runtime, manager);
hid.set_output_report_handler(
    [](std::string_view id, const uint8_t* data, std::size_t size) {
        return platform_write_report(id, data, size);
    });
hid.set_feature_report_handler(
    [](std::string_view id, const uint8_t* data, std::size_t size) {
        return platform_write_feature_report(id, data, size);
    });
hid.set_feature_report_reader(
    [](std::string_view id, uint8_t reportId, uint8_t* data,
       std::size_t size) {
        return platform_read_feature_report(id, reportId, data, size);
    });
hid.set_error_handler([](const insys::backend::HidError& error) {
    log_hid_error(error.code, error.deviceId, error.message);
});

insys::hid::DeviceInfo info;
info.id = platform_path;
info.vendorId = vendor;
info.productId = product;
info.description.interfaceName = "HID";
info.description.product = productName;
info.transport = insys::hid::HidTransport::Bluetooth;
info.reportDescriptor = rawDescriptor;
hid.add_device(std::move(info));

// 平台读取线程/轮询：
hid.feed_report(platform_path, report, reportSize);
hid.poll();               // 每个设备每帧最多一条合并状态
manager.update();
```

- `feed_report` 只更新 slot 和对应 report 的合并状态区间，`poll()` 才入队
  一条完整状态事件；同一帧多个 report id 先合并，避免同一 Device 在同一
  update step 被多次翻转状态缓冲。
- 平台可通过 `info.reportDataIncludesId=false` 让 backend 为无 report id
  的 input report 补上描述符 id，并按 payload 大小选择 report slot。
- `HidBackend` 使用 `Runtime::add_device_command_handler` 注册命令回调，
  与 XInput 的 legacy handler 共存。
- 产品布局会把 layout 继承链写入 `Device::layoutLineage`；控制方案和
  `Manager::device_by_layout("Gamepad")` 都能把 DualSense/DualShock/Switch
  设备视为 Gamepad。
- `send_output_report` / `send_feature_report` / `read_feature_report` 和对应
  handler 是平台 I/O 接口；Windows 由 `WindowsHid` 绑定 SetupAPI、读取线程、
  `HidD_SetOutputReport`/`HidD_SetFeature`/`HidD_GetFeature`，Linux 由
  `LinuxHid` 绑定 hidraw/`HIDIOCSFEATURE`/`HIDIOCGFEATURE`，macOS 由
  `MacOsHid` 绑定 IOHIDManager 和 `IOHIDDeviceSetReport/GetReport`。
- 平台写入失败会返回 false 并触发一次 `HidError::WriteFailed`；平台层负责
  短重试。输入队列溢出触发 `ReportQueueOverflow`，描述符/重复设备错误分别
  触发 `InvalidReport`/`DuplicateDevice`。

## 验证

- `insys_tests`：**287 cases / 3493 assertions**。
- 新增覆盖：descriptor 解析、generic layout 轴归一化/按钮/semantic dpad、
  DS4 风格多轴顺序的右摇杆/扳机映射、产品布局、DS4/DS5 full report
  touch+motion、DS4/DualSense Bluetooth 输入 CRC（seed 0xA1）与输出 CRC
  （seed 0xA2）、Switch full report IMU、adaptive trigger 预设、Switch
  subcommand/参数 helper、HID backend 多 report 合并设备/动态 report id/
  raw output/feature write/feature read/错误上报/去重与 transport、Switch
  handshake（含 IMU 启用）、报告队列背压/丢弃/移除、DS3 解码/震动和
  Steam Deck feature-report 震动。

限制：当前无真实 HID 手柄可用于 Windows/Linux/macOS 实机验收；Steam Input API
需要外部 Steamworks SDK；DualSense trigger 的实际手感、Switch 完整
subcommand 集和 Bluetooth input/output CRC 仍需实机确认。macOS IOHID 源
已接入但未在 macOS 编译/运行验证；Linux hidraw 的 transport/feature-report
路径也未在当前 Windows CI 编译验证。











