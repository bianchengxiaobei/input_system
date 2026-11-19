# C++ Input System 实施文档索引

本目录是把 `../CppInputSystem_ImplementationPlan.md` 按里程碑拆分后的**逐步实施文档**。
每个里程碑文档都是自包含的：目标、前置条件、任务清单、详细步骤、接口草案、测试与验收标准。

## 文档列表

| 文档 | 内容 | 依赖 |
|---|---|---|
| [00-总体设计](../CppInputSystem_ImplementationPlan.md) | 架构、数据结构、算法、Unity 源码对照 | 无 |
| [M0-Core-Skeleton.md](M0-Core-Skeleton.md) | 工程骨架、事件/状态层、GLFW 键鼠、最小 Demo | 无 |
| [M1-Layout-Gamepad.md](M1-Layout-Gamepad.md) | 布局系统、ControlPath、DeviceBuilder、手柄 | M0 |
| [M2-Actions-Basics.md](M2-Actions-Basics.md) | Action/Map/Asset、BindingResolver、ActionState、默认交互 | M1 |
| [M3-Interactions-Rebinding-Assets.md](M3-Interactions-Rebinding-Assets.md) | Interaction、Composite、Processor、重绑定、自定义资产 | M2 |
| [M4-RmlUi-Integration.md](M4-RmlUi-Integration.md) | RmlUi 输入适配、SystemInterface、焦点门控、文本/IME | M1（输入）、M2/M3（Action 导航） |
| [M5-Debugging-Stability.md](M5-Debugging-Stability.md) | RmlUi 调试器、事件 Trace/回放、性能与稳定性 | M4 |
| [M6-Enhancements.md](M6-Enhancements.md) | Raw Input、XInput、跨平台、二进制资产 | M5 |
| [M7-Platform-Ecosystem.md](M7-Platform-Ecosystem.md) | HID 解析、DualShock/DualSense/Switch/Steam、haptics、Windows/Linux/macOS HID 平台源 | M6 |
| [M8-Editor.md](M8-Editor.md) | 独立输入资产编辑器：Web 前端、C++ 服务、绑定捕获、运行时预览、HID 工具 | M3、M5、M7 |

## 里程碑依赖图

```
M0 ──► M1 ──► M2 ──► M3 ──► M4 ──► M5 ──► M6 ──► M7
                 │           ▲
                 └───────────┘
```

- M4 的**指针/键盘/文本**部分只依赖 M1；**手柄导航（Action 驱动）**依赖 M2/M3。
- 建议严格按顺序推进，每个里程碑结束都能运行 Demo 并完成验收。

## 完成定义（Definition of Done）

每个里程碑只有同时满足以下条件才算完成：

1. 任务清单（`- [ ]`）全部勾选。
2. `ctest` 全绿。
3. Demo 可运行，且该里程碑的验收项手动通过。
4. 新增公共接口有注释；关键算法有单元测试。
5. 无编译警告（`-Wall -Wextra`，MSVC `/W4`）。
6. ASan/Valgrind 下无泄漏与越界（M0 起持续保持）。

## 推荐工作方式

1. 先读 `00-总体设计` 对应章节，再读当前里程碑文档。
2. 按文档中的“步骤”顺序实现，每完成一个步骤就写/跑对应测试。
3. 不要提前实现后续里程碑的内容，避免返工。
4. 每完成一个里程碑，在下方进度表打勾。

## 进度表

- [x] M0 核心骨架
- [x] M1 布局系统 + 手柄
- [x] M2 Actions 基础
- [x] M3 交互 / 重绑定 / 资产
- [x] M4 RmlUi 集成
- [x] M5 调试与稳定性
- [ ] M6 增强项（Raw Input / XInput / M6.3 核心处理器与合成器、死区曲线、平滑、基础手势 / 6.1.4 Linux evdev 映射/聚合/事件解码/能力位分类/采集会话/热插拔差异/后端集成、`/dev/input` 源、inotify watcher 及 macOS GameController 映射/后端 / 6.1.5 二进制资产 / 6.1.6 核心设备、Windows 触摸、传感器后端、触摸后端、移动端桥与事件队列 / 6.1.7 远程输入核心、TCP/UDP/WebSocket 传输已完成，其余可选项未做）
- [x] M7 平台外设生态（HID 报告解析、generic 布局与 DS4 风格摇杆/扳机语义映射、DS3/DS4/DS5/Switch/Steam/Steam Deck 编解码、touchpad/IMU、双马达 haptics、DS4/DS5 lightbar、DS5 adaptive trigger 预设、Bluetooth 输入/输出 CRC、Switch 完整 subcommand、HidBackend 多 report 合并/动态 report id/raw output 与 feature 读写/错误上报、Windows/Linux/macOS HID 枚举/读取/写入/transport 检测/去重/队列背压/写重试；Steam Input API 待外部 Steamworks 集成，macOS IOHID 待实机编译验证）
- [x] M8 E0 基础骨架（`insys_hid_platform` 拆分、Web+C++ 服务、token/Origin、`asset.open/new/validate/save`、SHA-256 revision、JSON `priority`、React 三栏 Raw JSON 页面、编辑器测试）
- [x] M8 E1 资产编辑 MVP（`catalog.get`、ActionMap/Action/Binding/Composite/ControlScheme CRUD、结构化 Inspector、控制方案 groups、undo/redo、复制粘贴、快捷键、语义诊断、Raw JSON 切换、前端 Vitest）
- [x] M8 E2 捕获与运行时预览（`PreviewRuntime`、150 ms 热更新、设备/Action/事件快照、键盘/鼠标注入、HID 预览、Listen 捕获、expected/device 过滤、derived bindings、Composite part 捕获、Preview UI）
- [x] M8 E3 HID 工具（HidBackend devices/report listener、descriptor/elements/reports/collections、generic layout、raw report monitor/pause/clear/JSONL、feature/output 写入、rumble/lightbar/adaptive trigger/Switch 子命令、HidError 与二次确认）
- [ ] M8 E4 工作流与打包（`.insysb` 导入/烘焙、schema 提示、偏好持久化、稳定序列化、1000+ 行虚拟列表、Vitest/C++ 覆盖、Windows WebView2 壳、外部浏览器入口、原生 Open Asset、auto-save、便携 ZIP、MSIX 与 Playwright E2E 已完成；macOS/Linux 原生壳代码已落地，编译/smoke 验证待做）

## 全局约定

- 命名空间：`insys`、`insys::low`、`insys::backend`、`insys::hid`、`insys::ui`。
- C++20，CMake。
- 核心库 `insys` 不依赖 GLFW / RmlUi / OpenGL。
- 所有时间使用“输入时间轴”（单调递增秒）。
- 核心库中不使用异常做控制流；错误用返回值 + 诊断日志。
- 公共头文件放 `include/insys/`，实现放 `src/`。

































