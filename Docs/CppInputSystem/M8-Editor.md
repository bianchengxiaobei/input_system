# M8 输入资产编辑器（Web 前端 + C++ 服务）

> 命名说明：M6 文档原本预留了 `M7-Editor.md`。M7 已用于平台外设生态，
> 因此本工作流正式编号为 M8，内容仍是独立输入资产编辑器。
>
> 参考实现：
> `Packages/com.unity.inputsystem/InputSystem/Editor/UITKAssetEditor/`、
> `InputSystem/Editor/ControlPicker/`、`InputSystem/Editor/AssetEditor/` 和
> `InputSystem/Editor/AssetImporter/`。
>
> 本文只对齐 Unity 编辑器的行为、校验规则和数据操作，不复制 Unity 的
> UIElements/C# 代码结构。

## 1. 目标

提供一个独立于游戏运行时的输入资产编辑器，覆盖 Unity Input Actions 编辑器的主要工作流：

- 打开、编辑、校验、保存 `ActionAsset` JSON。
- 编辑 ActionMap、Action、Binding、Composite、Interaction、Processor、ControlScheme。
- 从真实设备捕获控件，生成 ControlPath。
- 实时预览设备状态、Action phase/value 和绑定解析结果。
- 查看 HID descriptor、generic layout 映射和原始 input/output/feature report。
- 通过现有 `asset_bake` 导出 `.insysb`。

编辑器采用 **Web 前端 + 本地 C++ 服务**：

```text
┌──────────────────────────────┐
│ Browser / WebView            │
│ React + TypeScript           │
│                              │
│ Action 树 / Inspector / 预览 │
│ HID 工具 / Raw JSON          │
└──────────────┬───────────────┘
               │ JSON-RPC over WebSocket
               │ 仅 127.0.0.1，随机 token
┌──────────────▼───────────────┐
│ insys_editor_server          │
│                              │
│ EditorSession                │
│  ├─ CatalogService           │
│  ├─ ValidationService        │
│  ├─ PreviewRuntime           │
│  ├─ CaptureService           │
│  ├─ HidToolService           │
│  └─ FileService              │
│                              │
│ insys / AssetLoader          │
│ insys_hid_platform           │
└──────────────────────────────┘
```

运行时 Debugger、游戏内 HUD 和 Demo 继续使用 RmlUi；不把运行时 UI 迁到 Web。

## 2. 非目标

首版不做：

- Unity Editor 插件或 Unity Project 集成。
- 场景编辑器、动画编辑器、Timeline 编辑器。
- 云端协作、多人实时编辑。
- 完整撤销树、版本控制客户端或资产合并算法。
- 直接编辑 `.insysb`。`.insysb` 作为只读导入和导出目标。
- 自动修改运行时已经加载的资产实例。
- 在没有用户确认的情况下发送任意 HID 输出或 feature report。

## 3. 关键决策

### 3.1 为什么用 Web

- 编辑器以树、表格、表单、文本、拖拽、diff 为主，Web 组件生态更成熟。
- 浏览器原生支持 IME，适合中文名称、备注、搜索。
- 复杂文本编辑、虚拟列表、拖拽、undo/redo、JSON diff 可直接复用成熟库。
- TypeScript 前端迭代速度明显快于 C++/RmlUi 每轮重编译。
- 前端与运行时解耦，UI 崩溃不会直接破坏游戏运行时。

### 3.2 为什么不是纯浏览器页面

浏览器不能直接访问本地 HID、hidraw、IOHID、feature report 或任意文件。
因此必须有本地 C++ 服务负责：

- 文件读写、路径白名单、原子保存和备份。
- 设备枚举、HID 报告、feature/output report。
- `insys::Manager` 和运行时预览。
- 构建期资产烘焙。

### 3.3 前端技术默认选型

- React + TypeScript + Vite。
- Zustand 管理编辑器状态。
- Monaco Editor 负责 Raw JSON 和差异查看。
- `@dnd-kit` 负责树和列表拖拽。
- `@tanstack/react-virtual` 负责大列表虚拟化。
- 前端只通过协议访问数据，不直接拼接本地文件路径。

如果团队已有 Vue/Svelte 基础设施，可替换前端框架；C++ 服务和协议不变。

## 4. 目录与构建目标

目录（E0 已落地）：

```text
CppInputSystem/
  include/insys/editor/
    editor_server.h
    editor_protocol.h
    editor_session.h
  src/editor/
    sha256.cpp
    editor_session.cpp
    editor_protocol.cpp
    editor_server.cpp
  tools/editor/
    main.cpp                     # insys_editor_server
  tools/editor_shell/
    main.cpp                     # Windows WebView2 shell
    main_macos.mm                # macOS WKWebView shell
    main_linux.cpp               # Linux WebKitGTK shell
    posix_shell.cpp              # shared POSIX CLI/server helpers
  editor/web/
    package.json
    vite.config.ts
    src/App.tsx
    src/api.ts
  tests/
    test_editor_session.cpp
    test_editor_protocol.cpp
    test_editor_server.cpp
```

E1–E3 继续增加：

```text
  src/editor/
    editor_catalog.cpp
    editor_validation.cpp
    editor_preview.cpp
    editor_capture.cpp
    editor_hid_tools.cpp
  tests/
    test_editor_catalog.cpp
    test_editor_validation.cpp
    test_editor_preview.cpp
```

新增 CMake 目标：

```cmake
option(INSYS_BUILD_EDITOR "Build the input asset editor server" OFF)
option(INSYS_BUILD_EDITOR_WEB "Build the web frontend" OFF)
```

目标关系：

```text
insys
  ▲
  ├── insys_hid_platform       # 从 GLFW 后端抽出的平台 HID
  ├── insys_editor_core        # 无 GLFW、无 RmlUi、无 Web 依赖
  └── insys_editor_server      # 本地 HTTP/WebSocket 服务
          ▲
          └── editor/web/dist  # 生产环境静态资源

insys_backend_glfw
  └── insys_hid_platform
```

`insys_hid_platform` 是必要重构：当前 `windows_hid.cpp`、`linux_hid.cpp`、
`macos_hid.cpp` 位于 `insys_backend_glfw` 目标内，编辑器服务不应为了 HID
而链接 GLFW、RmlUi 或 Demo。

## 5. 文档模型

### 5.1 前端持有编辑文档

首版采用：

- 前端持有 canonical JSON AST。
- 后端不维护长期文档状态，只提供服务：
  - 打开文件并返回 JSON 文本和 revision。
  - 校验 JSON。
  - 预览 JSON。
  - 保存 JSON。
  - 烘焙 JSON。

原因：

- 资产通常小于几 MB，整文档同步成本可控。
- 撤销/重做、选择状态、拖拽状态都在前端，避免每次编辑都跨进程往返。
- 后端保持无状态，崩溃不会丢失前端未保存内容。
- 后续需要多人协作时，再把 canonical document 移到后端。

前端文档状态：

```ts
interface EditorDocument {
  path: string | null;
  revision: string;        // 文件内容 SHA-256 或打开的 revision
  json: JsonObject;        // canonical asset AST
  dirty: boolean;
  selection: Selection;
  undoStack: Command[];
  redoStack: Command[];
  diagnostics: Diagnostic[];
}
```

### 5.2 现有 JSON 字段审计

当前 `AssetLoader` 的 JSON 格式与运行时的二进制格式并不完全等价。编辑器开写前必须先处理这些差异：

| 字段 | JSON 当前状态 | 二进制当前状态 | 编辑器策略 |
|---|---|---|---|
| `ActionDef.priority` | E0 已序列化 | 已序列化 | JSON 使用可选 `priority`，0 时省略 |
| `Binding.overridePath` | 未序列化 | 已序列化 | 首版不编辑；运行时 override 由应用层管理 |
| `Binding.overrideInteractions` | 未序列化 | 已序列化 | 首版不编辑 |
| `Binding.overrideProcessors` | 未序列化 | 已序列化 | 首版不编辑 |
| `ActionAsset.devices` | 未序列化 | 未序列化 | 首版不编辑 |
| `ActionAsset.bindingMask` | 未序列化 | 未序列化 | 首版不编辑 |
| `ControlScheme.devices` | 字符串数组 | 字符串数组 | 编辑布局/设备要求字符串 |
| 每设备 `isOptional` | 当前 `ControlScheme` 不支持 | 不支持 | 首版只支持 scheme 级 `isOptional` |
| 未知 JSON 字段 | 严格拒绝 | 不适用 | 首版严格模式；后续增加 schema 版本和保留策略 |

E0 阶段必须完成：

- 给 `parse_action`/`serialize_action` 增加 `priority`。
- 给 `check_keys` 增加 `priority`。
- 增加 JSON 往返测试，确认 `priority` 不再丢失。
- 如果决定编辑 override 字段，再为 binding 增加三个可选 JSON 字段；否则在 UI 中明确标记为运行时字段。

### 5.3 校验分层

校验分三层：

1. **JSON 语法层**
   - 括号、逗号、字符串转义。
   - BOM、CRLF、UTF-8 处理。
   - 返回 `line`、`column`。

2. **Schema 层**
   - 复用 `AssetLoader::load_from_json` 的严格字段检查。
   - 版本号、必填字段、类型、重复 ActionMap/Action/ControlScheme。
   - action 引用存在、composite 可创建、interaction/processor 可解析。

3. **语义层**
   - 重复 GUID。
   - 空 Action、无绑定 Action（warning）。
   - binding path 无法被任何 layout 匹配（warning）。
   - composite 缺 part、part 名未知、part 数不匹配（error/warning）。
   - binding group 与 ControlScheme 的 `bindingGroup` 不一致（warning）。
   - ControlScheme 设备要求无法匹配（warning）。
   - expected control type 与 path 控件 layout 不一致（warning）。

诊断结构：

```ts
interface Diagnostic {
  severity: "error" | "warning" | "info";
  code: string;
  message: string;
  jsonPointer?: string;
  line?: number;
  column?: number;
  related?: { jsonPointer: string; message: string }[];
}
```

### 5.4 撤销、重做、复制粘贴

前端命令模式：

```ts
interface EditorCommand {
  apply(doc: EditorDocument): EditorDocument;
  invert(doc: EditorDocument): EditorCommand;
  label: string;
}
```

要求：

- 所有会修改资产的操作都走命令，包括重命名、拖动排序、修改字段。
- `undoStack` 上限默认 200，超过后丢弃最旧记录。
- 选择状态不进入 undo；但删除/移动后要修正选择索引。
- 复制粘贴使用自定义 JSON 标记，例如：
  ```text
  INPUTASSET1 action { ... }
  INPUTASSET1 binding { ... }
  ```
- 复制 Action 时一并复制其 Binding 和 Composite part。
- 复制 Composite 时保持 part 顺序和 `partIndex`。
- 粘贴到不同 ActionMap 时保留 action 名称，必要时生成唯一名称。
- 复制到同一 ActionMap 时自动生成唯一 ID 和名称。

### 5.5 文件修改冲突

- 打开时记录内容 SHA-256。
- 保存时发送 `expectedRevision`。
- 服务端发现磁盘 revision 不同：
  - 前端干净：提示后重新加载。
  - 前端有未保存修改：返回 `ASSET_CONFLICT`，要求覆盖、另存为或重新加载。
- 保存采用临时文件 + fsync + rename，并保留 `.bak`。
- 支持可选 autosave，默认关闭；autosave 不覆盖外部修改。

## 6. C++ 后端

### 6.1 EditorSession

`EditorSession` 是单个 WebSocket 连接的会话，负责：

- 协议握手和版本检查。
- 文件打开/保存/烘焙。
- 持有当前预览实例。
- 持有当前捕获状态。
- 持有 HID 工具状态。
- 向前端推送合并后的状态事件。

线程模型：

```text
WebSocket IO 线程
  ├─ 接收 JSON-RPC 请求
  ├─ 解析、鉴权、入队
  └─ 发送通知

Editor worker 线程
  ├─ 文件 I/O
  ├─ Manager / ActionAsset
  ├─ HID / preview / capture
  └─ 生成只读快照

WebSocket IO 线程 ◄── 有界快照队列 ◄── Editor worker 线程
```

约束：

- `Manager`、`ActionAsset`、`HidBackend` 只在 editor worker 线程访问。
- WebSocket 线程不直接调用 `Manager`。
- 高频状态合并后推送，默认 30 Hz；原始 HID report 可单独 10–16 ms 批量推送。
- 快照队列有上限；慢客户端只保留最新快照，避免无限内存。

### 6.2 CatalogService

Catalog 提供编辑器需要的静态元数据：

- layouts：名称、displayName、extends、controls。
- controls：path、layout、displayName、usages、aliases、isButton/isAxis。
- control types：`Button`、`Axis`、`Vector2`、`Vector3`、`Quaternion`、`Integer`、`Double`、`Stick` 等。
- processors：名称、参数提示。
- interactions：名称、参数提示。
- composites：名称、part 列表、参数提示。
- action types、binding group 示例。

实现方式：

- 用 `LayoutRegistry::list()` 遍历 layout。
- 用 `DeviceBuilder` 构建临时 Device，遍历 `Device::allControls` 或 `childrenFlat`，生成 control path。
- 过滤 `hideInUI` 类似行为时，使用 `ControlItem` 的 display 信息和 layout 继承。
- 首版 processors/interactions/composites 的名称和参数提示放在 editor catalog 表中；测试确保名称能被 `make_processor`、`make_interaction`、`make_composite` 接受。
- 后续可在 core 增加 registry introspection，避免维护重复表。

### 6.3 ValidationService

`validate(json, options)`：

1. 调用 `AssetLoader::load_from_json`，把 `AssetError` 转为诊断。
2. 遍历 `ActionAsset`：
   - GUID 唯一性。
   - Action 名称、Binding 引用、Composite part 关系。
   - ControlScheme、binding groups、设备要求。
3. 使用 Catalog 检查 path 是否能匹配至少一个 layout/control。
4. 返回结构化诊断，不抛异常给 WebSocket 层。

注意：

- `AssetLoader` 当前遇到未知字段会直接失败；首版沿用严格模式。
- 不要用 `AssetLoader::to_json` 直接覆盖用户原文；保存时应基于前端 AST 序列化，或至少确保所有已支持字段往返一致。
- 保存前必须再校验一次；校验失败返回 `VALIDATION_FAILED`，不写文件。

### 6.4 PreviewRuntime

预览复用现有运行时，不新造输入系统：

- `insys::backend::Runtime` 作为无窗口 IRuntime。
- `Manager` + `ActionAsset::enable`。
- `DebuggerModel` 生成设备、控件、Action、绑定和最近事件快照。
- `HidBackend` + `insys_hid_platform` 接入手柄。
- Web 键盘/鼠标事件通过协议注入：
  - `preview.input.key`：`code`、`pressed`、`repeat`。
  - `preview.input.mouse`：位置、按键、滚轮、delta。
  - C++ 侧映射到 `Key`/Mouse 事件；不能假设浏览器 key 与 GLFW key 相同，需要独立映射表。
- 预览资产变更使用 debounce（建议 150 ms）：
  1. 前端发送完整 JSON + generation。
  2. 后端校验。
  3. 停用旧 ActionAsset，加载新资产，重新 enable。
  4. 保留设备和 HID 状态，避免每次改绑定都重新打开设备。

预览快照：

```ts
interface PreviewSnapshot {
  generation: number;
  devices: DeviceSnapshot[];
  actions: ActionSnapshot[];
  bindings: BindingSnapshot[];
  events: InputEventSnapshot[];
  diagnostics: Diagnostic[];
}
```

### 6.5 CaptureService

捕获分两类：

1. **键盘/鼠标**
   - Web 前端监听 `keydown`、`mousedown`、`wheel`、`pointermove`。
   - 只发送规范化后的 `code`/button/wheel，不发送原始页面事件对象。
   - 前端直接生成候选 path，后端用 Catalog 校验。
   - 监听模式要屏蔽编辑器快捷键，`Esc` 取消。

2. **手柄/HID**
   - 使用 `HidBackend`/`Manager` 的设备状态变化。
   - 监听控件值跨越阈值，生成 `<Layout>/<controlPath>`。
   - 默认按钮阈值 0.5，轴阈值 0.3；可用参数调整。
   - 忽略噪声控件（`isNoisy`）和 synthetic 控件。
   - 同一路径 100 ms 去抖，避免扳机/摇杆连续触发。

捕获结果：

```ts
interface CapturedControl {
  path: string;
  deviceId: string;
  controlLayout: string;
  value: number;
  isButton: boolean;
  timestamp: number;
}
```

### 6.6 HidToolService

依赖 `HidBackend` 和 `insys_hid_platform`，提供：

- 设备列表：id、product、manufacturer、serial、transport、kind、usage、report sizes。
- descriptor 查看：
  - 原始字节。
  - 解析后的 elements、collections、report id/type/size。
  - Generic layout 生成的语义控件和 state offset。
- 原始 report 监视：
  - input report id、size、hex、timestamp、频率。
  - 可选监听 output/feature report。
  - ring buffer 默认每设备 500 条。
- feature report 读取/写入。
- output report 写入。
- 震动、lightbar、DualSense adaptive trigger、Switch subcommand 测试面板。
- `HidError` 实时日志和权限提示。

现有 `HidBackend` 需要补充只读接口：

```cpp
struct DeviceView {
    std::string id;
    hid::DeviceInfo info;
    hid::DeviceKind kind;
};

std::vector<DeviceView> devices() const;
void set_report_listener(ReportListener listener);
```

`ReportListener` 只接收拷贝后的 report，不允许回调中直接修改 backend 状态。

### 6.7 协议

使用 JSON-RPC 2.0 over WebSocket。

请求示例：

```json
{"jsonrpc":"2.0","id":1,"method":"asset.open","params":{"path":"assets/player.json"}}
```

响应示例：

```json
{"jsonrpc":"2.0","id":1,"result":{"path":"assets/player.json","revision":"sha256:...","json":"{...}"}}
```

错误示例：

```json
{"jsonrpc":"2.0","id":1,"error":{"code":-32010,"message":"asset validation failed","data":{"diagnostics":[...]}}}
```

方法：

| 方法 | 参数 | 返回 |
|---|---|---|
| `session.hello` | `protocolVersion`, `client` | `serverVersion`, `tokenValid` |
| `dialog.open` | `filters` | `path?` |
| `dialog.save` | `filters`, `suggestedName` | `path?` |
| `asset.open` | `path` | `path`, `revision`, `json`, `format`, `readOnly`, `schemaVersion?` |
| `asset.new` | `name?` | `json` |
| `asset.validate` | `json` | `diagnostics` |
| `asset.save` | `path`, `json`, `expectedRevision`, `createBackup` | `path`, `revision`, `changed` |
| `asset.bake` | `path`, `json`, `expectedRevision?`, `createBackup?` | `path`, `revision`, `bytes`, `changed` |
| `catalog.get` | 无 | `layouts`（含 controls）, `devices`, `controlTypes`, `composites`, `interactions`, `processors` |
| `catalog.controlPaths` | `query`, `expectedControlType?`, `deviceFilter?` | `paths[]` |
| `preview.start` | `json?` | `generation` |
| `preview.update` | `json`, `generation` | `generation` |
| `preview.stop` | 无 | `ok` |
| `preview.snapshot` | 无 | `generation`, `devices`, `actions`, `bindings`, `events`, `captured` |
| `preview.input.key` | `code`, `pressed`, `repeat` | `ok`, `recognized` |
| `preview.input.mouse` | `x`, `y`, `button?`, `pressed?`, `wheelX?`, `wheelY?`, `deltaX?`, `deltaY?` | `ok`, `changed` |
| `capture.start` | `expectedControlType?`, `deviceFilter?` | `captureId` |
| `capture.stop` | `captureId` | `ok` |
| `hid.devices` | 无 | `DeviceView[]` |
| `hid.descriptor` | `deviceId` | `parsed`, `raw`, `elements`, `reports`, `collections`, `genericLayout`, `device` |
| `hid.reports.start` | `deviceId`, `types`（`input`/`output`/`feature`） | `monitorId` |
| `hid.reports.poll` | `monitorId`, `max?` | `reports[]`, `dropped`, `paused` |
| `hid.reports.pause` | `monitorId`, `paused` | `ok`, `paused` |
| `hid.reports.clear` | `monitorId` | `ok` |
| `hid.reports.stop` | `monitorId` | `ok` |
| `hid.feature.read` | `deviceId`, `reportId`, `size` | `bytes` |
| `hid.feature.write` | `deviceId`, `bytes`, `confirmed: true` | `ok` |
| `hid.output.write` | `deviceId`, `bytes`, `confirmed: true` | `ok` |
| `hid.haptics` | `deviceId`, `left`, `right`, `confirmed: true` | `ok` |
| `hid.lightbar` | `deviceId`, `r`, `g`, `b`, `confirmed: true` | `ok` |
| `hid.adaptiveTrigger` | `deviceId`, `side`, `mode`, `params`, `confirmed: true` | `ok` |
| `hid.switch.subcommand` | `deviceId`, `subcommand`, `payload`, `confirmed: true` | `ok` |
| `hid.errors` | 无 | `errors[]` |
| `hid.errors.clear` | 无 | `ok` |
| `app.shutdown` | 无 | `ok` |

通知：

| 通知 | 内容 |
|---|---|
| `preview.snapshot` | 合并后的设备/Action/绑定/事件快照（E2 前端以 30 Hz 请求 `preview.snapshot` 轮询） |
| `capture.control` | 捕获到的控件 |
| `hid.report` | 批量原始报告（E3 前端通过 100 ms `hid.reports.poll` 轮询） |
| `hid.error` | `HidError`（E3 前端通过 1 s `hid.errors` 轮询） |
| `asset.externalChanged` | 文件被外部修改 |
| `log` | 服务端日志 |
| `server.shutdown` | 服务端关闭 |

协议约束：

- 请求大小默认上限 16 MB。
- 每连接最多一个未完成的 save/bake。
- WebSocket 消息必须 UTF-8。
- 所有路径使用绝对路径或配置白名单内的相对路径。
- 未知方法返回 `METHOD_NOT_FOUND`，未知字段忽略并记录 warning。

### 6.8 平台 HID 重构

当前平台 HID 源在 `insys_backend_glfw` 内：

- `src/backend/windows/windows_hid.cpp`
- `src/backend/linux/linux_hid.cpp`
- `src/backend/macos/macos_hid.cpp`

重构步骤：

1. 新增 `insys_hid_platform` 静态库，只包含上述平台 HID 源和头文件。
2. `insys_backend_glfw` 链接 `insys_hid_platform`，保持现有 Demo 行为。
3. `insys_editor_server` 直接链接 `insys_hid_platform`，不依赖 GLFW/RmlUi。
4. 平台 HID 的 feature reader/error handler 接线保持现状。
5. 增加 Windows/Linux/macOS 编译验证；当前 Windows 环境只能验证 Windows 分支。

### 6.9 文件服务

- `AssetLoader::load_from_file` 只支持 `.json`/`.insysb`；编辑器可打开两者。
- `.insysb` 通过 `AssetLoader::load_from_binary` 导入为内存 JSON，标记为只读；可另存为 `.json` 或通过 `asset.bake` 导出新的 `.insysb`。
- JSON 保存时：
  1. 校验 JSON。
  2. 写临时文件。
  3. 写 `.bak`。
  4. rename 覆盖。
  5. 计算 SHA-256 revision。
- `.insysb` 烘焙复用 `AssetLoader::to_binary`，同样原子写入并可创建 `.bak`；已存在目标必须提供 revision，前端确认覆盖时会重新打开目标取得 revision。
- 最近文件、窗口布局、预览轮询频率等编辑器状态写入 localStorage 独立配置，不写进资产。
- 所有文件路径必须经过白名单和规范化，拒绝 `..` 逃逸和符号链接逃逸。

## 7. 前端界面

### 7.1 主布局

```text
┌────────────────────────────────────────────────────────────┐
│ 工具栏：打开 保存 另存为 撤销 重做 方案筛选 设备筛选 搜索 │
├───────────────┬──────────────────────────┬─────────────────┤
│ Action Maps   │ Actions / Bindings       │ Inspector       │
│ + Control     │ 树/表格                  │ Action/Binding  │
│   Schemes     │                          │ Composite       │
├───────────────┴──────────────────────────┴─────────────────┤
│ 校验结果 / 状态栏 / 未保存提示                             │
└────────────────────────────────────────────────────────────┘
```

页面标签：

- **Asset**：ActionMap、Action、Binding、ControlScheme。
- **Preview**：设备、控件、Action 状态、事件流。
- **HID**：设备列表、descriptor、raw report、feature/output 测试。
- **Raw JSON**：Monaco 编辑器、格式化、校验、定位错误。

### 7.2 交互

- ActionMap/Action/Binding 支持右键菜单：新增、重命名、复制、剪切、粘贴、复制副本、删除。
- 支持拖拽排序和跨 ActionMap 移动；非法移动显示禁止光标。
- 新 Action/ActionMap 创建后自动进入重命名状态。
- Binding 支持 composite 折叠/展开；part 可单独选择。
- Inspector 根据选中项切换：
  - Action：type、control type、priority、initial state check、interactions、processors。
  - Binding：path、groups、interactions、processors、use in control scheme。
  - Composite：类型、参数、parts。
  - ControlScheme：名称、binding group、设备要求、optional。
- 控制方案筛选影响 Actions 树显示，但不修改资产。
- 设备筛选只影响 path picker 和预览，不修改绑定。

### 7.3 控件选择器

- 显示 device/layout 树、usages、controls。
- 支持搜索 path、layout、usage。
- 支持按 expected control type 过滤。
- 支持 Listen 捕获。
- 显示匹配的 derived bindings。
- 选中后生成规范化 path，例如 `<Gamepad>/leftStick/x`。
- 支持手动输入 path；输入时实时校验并显示错误。

### 7.4 状态与错误

- dirty 状态在标题和工具栏显示。
- 保存失败显示具体文件错误、校验错误或冲突。
- WebSocket 断开时保留本地文档，重连后提示同步。
- 预览失败不阻塞编辑；显示诊断并保留上一个可用预览。
- HID 工具的错误以设备为单位显示，避免全局弹窗。

## 8. 实施阶段

### E0：基础骨架与前置修正

目标：能打开一个 JSON、编辑、校验、保存，并跑通 Web ↔ C++ 协议。

- [x] 抽取 `insys_hid_platform`，保持 Demo 和现有测试通过。
- [x] 增加 `INSYS_BUILD_EDITOR`/`INSYS_BUILD_EDITOR_WEB` CMake 选项。
- [x] 新增 `insys_editor_core` 静态库和 `insys_editor_server` 可执行文件。
- [x] 实现 `session.hello`、`asset.open`、`asset.new`、`asset.validate`、`asset.save`。
- [x] 增加 JSON `priority` 字段的解析/序列化和往返测试。
- [x] 实现 JSON-RPC 编解码、错误码、请求大小限制。
- [x] 实现本地 token、Origin 检查、127.0.0.1 绑定。
- [x] React 页面骨架：工具栏、三栏布局、Raw JSON、诊断面板。
- [x] 开发模式 Vite proxy；生产模式 server 提供 `dist`。
- [x] 用临时资产完成打开→编辑→保存→重新打开测试。

验收：

- `insys_tests`、UI tests、Demo 仍通过。
- 不启用 editor 选项时原构建完全不变。
- 编辑器能保存一个字段变化且重新加载后值正确。
- 未知方法、坏 JSON、超大请求都有明确错误。

E0 实现状态（当前工作区已完成）：

- `insys_hid_platform` 已从 `insys_backend_glfw` 拆出；Windows HID 目标只链接 `insys` 与系统 `setupapi`/`hid`。
- `cmake --preset editor`、`cmake --build --preset editor-debug` 可构建 `insys_editor_core`、`insys_editor_server` 和 `insys_editor_tests`。
- `insys_editor_server --asset-root <dir>` 绑定 `127.0.0.1:17800`，打印带 token 的 URL；生产静态目录默认指向 `editor/web/dist`。
- 已实现 `session.hello`、`asset.open`、`asset.new`、`asset.validate`、`asset.save`、`app.shutdown`。
- 保存使用 SHA-256 revision、预期 revision 冲突检查、同目录临时文件 + 原子替换、可选 `.bak` 备份；未变化时不重写文件。
- 前端 `editor/web` 已通过 `npm run build`，包含工具栏、Asset 树、Raw JSON、Diagnostics 三栏骨架。
- 当前测试：`insys_tests` 288 cases / 3500 assertions，`insys_ui_tests` 15 cases / 133 assertions，`insys_editor_tests` 23 cases / 311 assertions；前端 `npm test` 16 cases。
- Windows Demo `--smoke --hid` 通过，说明 HID 拆分未改变 Demo 行为。

### E1：资产编辑 MVP

目标：覆盖 Unity Input Actions 编辑器的主要 CRUD。

- [x] CatalogService：layouts、controls、control types、composites、interactions、processors。
- [x] ActionMap 新增/重命名/复制/删除/排序。
- [x] Action 新增/重命名/复制/删除/排序；type、control type、priority、initial state check。
- [x] Binding 新增/编辑/复制/删除/排序；path、name、groups、interactions、processors。
- [x] Composite 新增/切换类型/编辑参数/编辑 parts/复制/删除。
- [x] ControlScheme 新增/编辑/复制/删除；binding group、设备要求、optional。
- [x] Binding 的 “Use in control scheme” 勾选；同步 groups。
- [x] 前端命令式 undo/redo、复制粘贴、快捷键。
- [x] 语义校验和诊断定位。
- [x] Raw JSON 与结构化编辑双向切换；JSON 格式化和错误定位。
- [x] 外部修改检测和保存冲突处理。
- [x] 原子保存、备份；autosave 保持可选且当前未启用。

验收：

- 可以创建一个完整资产：2 个 ActionMap、Value/Button/PassThrough、复合绑定、交互/处理器、2 个 ControlScheme。
- 保存后 `AssetLoader` 可加载；运行时 `enable` 后行为正确。
- 所有 CRUD 可撤销/重做。
- 非法 path、未知 composite/processor、重复 GUID、缺 part 均能定位。

E1 实现状态（当前工作区已完成）：

- 新增 `catalog.get`：server 从默认 layout registry、interaction registry 和内置 descriptor 表生成 layouts、controls、controlTypes、composites、interactions、processors。
- 前端 `asset.ts`/`commands.ts` 实现 canonical AST、快照式命令历史（上限 200）、ActionMap/Action/Binding/Composite/ControlScheme CRUD、复制粘贴与快捷键。
- 结构化 Inspector 支持 ActionMap、Action、Binding、Composite 参数/parts、ControlScheme 和 “Use in control scheme”；排序当前使用 `^`/`v` 按钮，未实现鼠标拖拽。
- Raw JSON 使用 textarea，支持 Apply/Format/Revert、JSON 行列错误提示和诊断到 Inspector 的定位；未引入 Monaco。
- 语义诊断覆盖未知 composite/缺失 part、空 path、控制方案 device/binding group；C++ 侧继续校验未知 path/composite/processor、重复 GUID 和 protocol 错误。
- 保存沿用 E0 的 revision/原子替换/`.bak`；切换保存路径时以空 revision 保护已存在文件，冲突提示使用 Reload；尚未做 autosave、覆盖确认弹窗和 Save As 对话框。
- 测试：`insys_editor_tests` 23 cases / 311 assertions；前端 `npm test` 16 cases；浏览器 smoke 已覆盖打开 sample、结构化编辑、undo/redo、复制粘贴、Raw JSON 应用和保存。

### E2：捕获与运行时预览

目标：从设备捕获绑定，并实时看到 Action 行为。

- [x] `PreviewRuntime`：headless Runtime + Manager + ActionAsset。
- [x] 预览资产热更新，保持设备连接。
- [x] 设备/控件/ Action/绑定/事件快照。
- [x] Web 键盘/鼠标事件映射到 C++ 输入事件。
- [x] HID 手柄接入预览。
- [x] Listen 捕获键盘、鼠标、手柄。
- [x] expected control type 过滤和 path 校验。
- [x] Derived bindings 列表。
- [x] Composite 分部捕获。
- [x] 捕获去抖、Esc 取消、快捷键屏蔽。
- [x] 预览页 UI：设备树、Action 状态、事件流、驱动控件。

验收：

- 手柄按钮/摇杆/扳机可被捕获为正确 path。
- 键盘/鼠标捕获不依赖 GLFW 窗口。
- 修改绑定后 150 ms 内预览更新。
- 同一控制器多 report 设备在预览中只出现一个 Device。

E2 实现状态（当前工作区已完成）：

- 新增 `include/insys/editor/editor_preview.h`、`src/editor/editor_preview.cpp`：`backend::Runtime` + `Manager` + `ActionAsset`，复用 `DebuggerModel` 生成设备、控件、Action、绑定和事件快照；热更新先解析新资产，再替换旧 `ActionAsset`，Keyboard/Mouse/HID 设备保持不变。
- 新增协议：`preview.start`、`preview.update`、`preview.stop`、`preview.snapshot`、`preview.input.key`、`preview.input.mouse`、`capture.start`、`capture.stop`；`preview.snapshot` 同时消费捕获结果。当前前端以 30 Hz 请求快照，而不是服务端推送通知。
- 浏览器 `code` 映射到 `Key`，鼠标位置/按键/滚轮/增量映射到 `MouseState`；不依赖 GLFW 窗口。`preview.input.mouse` 使用 `x`、`y`、`button`、`pressed`、`wheelX`、`wheelY`、`deltaX`、`deltaY`。
- HID 使用 `HidBackend` + `insys_hid_platform`；同一控制器多 report 由现有 `HidBackend` dedup 逻辑合并为一个 Device。
- 捕获支持键盘、鼠标按钮/滚轮/移动和手柄按钮/轴：按钮阈值 0.5、轴阈值 0.3、同 path 100 ms 去抖；忽略 noisy/synthetic 控件。`expectedControlType` 和 `deviceFilter` 生效。
- 新增 `insys::control_path(const Control&)`，生成 `<Layout>/control` 规范路径；DebuggerModel 和捕获结果统一使用该路径。
- 新增 `catalog.controlPaths`（query/expectedControlType/deviceFilter），供路径选择与校验复用。
- 前端新增 Preview 页：设备树、Action 状态、事件流、鼠标驱动区、Drive input、捕获提示/Esc 取消；Inspector 的 Binding path 和 Composite part 增加 Listen 按钮，并显示 derived bindings、路径匹配/expected control type 警告。
- 预览修改 debounce 150 ms；浏览器 smoke 已覆盖启动预览、键盘驱动 Jump、热更新绑定 `<Keyboard>/space` → `<Keyboard>/enter`、Listen 捕获和 HID 状态显示。
- 测试：`insys_editor_tests` 23 cases / 311 assertions；前端 `npm test` 16 cases。限制：手写合成 `<Gamepad>/leftStick/x` 这类 Vector2 子控件路径仍依赖运行时控件树，当前布局没有 x/y 子控件，因此复合分部捕获手柄摇杆时捕获父 path；快照为轮询而非通知。

### E3：HID 工具

目标：把 M7 HID 能力变成可视化验收工具。

- [x] `HidBackend::devices()` 只读快照。
- [x] `HidBackend::set_report_listener`。
- [x] HID 设备列表、transport、usage、report size。
- [x] Descriptor 原始字节和解析树。
- [x] Generic layout 语义映射预览（leftStick/rightStick/trigger/dpad）。
- [x] 原始 input report 监视、暂停、清空、导出 JSONL。
- [x] Feature report 读取/写入。
- [x] Output report 写入。
- [x] 震动/lightbar/adaptive trigger/Switch subcommand 测试面板。
- [x] `HidError` 日志和权限提示。
- [x] 所有写入操作二次确认，避免误发。

验收：

- Windows 上能枚举 HID 手柄并查看真实 descriptor。
- DS4/DualSense/Switch/Steam Deck 至少各有一个设备可显示 report 流。
- 发送 rumble/lightbar/adaptive trigger 不崩溃；失败有明确 `HidError`。
- 原始报告监视不影响正常预览。

E3 实现状态（当前工作区已完成）：

- `HidBackend` 新增 `DeviceView`/`devices()`、`HidReport`/`set_report_listener` 和近期 `HidError` 缓存；input report 在解码/去重前拷贝通知，成功的 output/feature 写入和 feature 读取也进入监听。
- 设备视图保存完整 `hid::DeviceInfo` 和 layout 名称；设备列表按 id 稳定排序，前端显示 transport、kind、usage、report 尺寸、descriptor 状态和能力位。
- descriptor 返回原始字节、elements、reports、collections；genericLayout 直接读取运行时 Device 的 `allControls`，显示 path、layout、byte/bit offset、size 和当前值。
- `HidToolService` 支持每 monitor 500 条 ring buffer、input/output/feature 类型过滤、暂停/恢复、清空、按批次轮询和 dropped 计数；前端 100 ms 轮询并以 JSONL 导出。
- 实现 `hid.devices`、`hid.descriptor`、`hid.reports.start/poll/pause/clear/stop`、`hid.feature.read/write`、`hid.output.write`、`hid.haptics`、`hid.lightbar`、`hid.adaptiveTrigger`、`hid.switch.subcommand`、`hid.errors`/`hid.errors.clear`。
- 所有写入协议要求 `confirmed: true`；前端先进入 Review 状态，再由独立 Confirm send 按钮提交，并显示设备、类型和字节数/参数摘要。
- HID 工具复用 PreviewRuntime 的 HidBackend；打开 HID 页可在没有资产/预览时启动 HID，后续 Preview 复用同一设备连接。
- 前端新增 HID Tools 页：设备列表、descriptor raw/tree/collections、generic layout、raw report 表、feature 读写、output 写入、rumble/lightbar/adaptive trigger/Switch 子命令和 HidError 面板。
- 测试：`insys_editor_tests` 23 cases / 311 assertions；前端 `npm test` 16 cases。HID 自动化测试使用 `enableHidPlatform=false` 的 fake backend；真实 DS4/DualSense/Switch/Steam Deck 的 descriptor/report 流仍需要 Windows 实机验收。

### E4：工作流与打包

目标：可日常使用的独立工具。

- [x] `.insysb` 导入（只读）和 `asset_bake` 导出。
- [x] 资产 schema 版本提示和迁移错误。
- [x] 最近文件、窗口布局、预览偏好持久化。
- [x] 文件 diff 友好的稳定序列化顺序。
- [x] 大资产虚拟列表和性能测试（1000+ bindings）。
- [x] Windows WebView2 打包。
- [x] 外部浏览器入口（File 菜单、`--open-browser`、`--browser-only`、`--browser PATH`）。
- [x] 原生 Open Asset 对话框与 Ctrl+O / Cmd+O（Windows IFileOpenDialog、macOS NSOpenPanel、Linux GtkFileChooser）。
- [x] Windows 便携 ZIP（CPack，含 MSVC runtime 和 web 资源）。
- [x] Windows MSIX（AppxManifest、makeappx、可选自签名开发证书）。
- [x] macOS WKWebView / Linux WebKitGTK 原生壳实现（编译/smoke 待平台验证）。
- [x] 可选 auto-save（失焦和关闭时保存，参考原编辑器 `autoSaveInputActionAssets`）。
- [x] 关闭窗口时处理未保存修改（Save / Discard / Cancel）。
- [ ] macOS WKWebView / Linux WebKitGTK 编译和人工 smoke。
- [x] 前端 Vitest + Playwright E2E（Chromium + WebView2 CDP）。
- [x] C++ 单测覆盖命令/校验/协议/预览。
- [x] 用户文档和已知限制。

验收：

- 不依赖浏览器手动启动，可作为桌面应用启动。
- 断网可用，所有资产本地处理。
- 基础编辑、预览、HID 工具在 Windows 实机通过。
- Linux/macOS 至少完成编译和人工 smoke。

E4 实现状态（Windows 工作流与 E2E 已完成，macOS/Linux 待验证）：

- `EditorSession::open` 支持 `.json` 和 `.insysb`；`.insysb` 通过 `AssetLoader::load_from_binary` 转为规范 JSON，打开结果为 `format=insysb`、`readOnly=true`、`schemaVersion`。
- `.insysb` 不能直接 `asset.save`；前端把源路径和建议 JSON 目标分开，允许另存为 `.json`，也允许保存到同名 JSON 时确认覆盖。
- 新增 `asset.bake` / `EditorSession::bake`，使用 `AssetLoader::to_binary` 原子写入 `.insysb`，返回绝对路径、revision、字节数和 changed；前端 Bake 按钮通过独立目标路径导出，并支持已有目标确认覆盖。
- schema 版本提示：JSON 版本错误返回 `asset.schema` 诊断和 migration 提示；二进制 header 版本错误返回 `-32025` / `schema_unsupported`，不静默迁移。
- 稳定序列化：C++ `AssetLoader::to_json` 和前端 `serializeAsset` 均固定 version/name/maps/actions/bindings/schemes 顺序；新增 C++/Vitest 回归测试。
- 偏好持久化：localStorage 保存最近文件、Active view、左右面板宽度和预览轮询频率；最近文件通过输入框 datalist 快速重开；面板宽度可拖拽分隔条调整。
- 虚拟列表：Action 树扁平化为固定 34 px 行，仅渲染可视窗口 + 6 行 overscan；1000 bindings 测试确认 DOM 行数保持在约 13 行，滚动仍可到达末尾。
- 原生桌面壳：Windows `tools/editor_shell/main.cpp`（WebView2）、macOS `main_macos.mm`（WKWebView）、Linux `main_linux.cpp`（WebKitGTK）都内嵌 `EditorServer`，先启动本地随机端口再创建窗口；仅放行 `http://127.0.0.1:<port>/` 导航，阻止弹窗，WebView2 不可用时回退外部浏览器。`posix_shell.cpp` 共享 CLI、server URL、日志和外部浏览器启动逻辑。
- 原生打开资产：Windows `File > Open Asset...`（Ctrl+O）使用 `IFileOpenDialog` 并通过 WebView2 `PostWebMessageAsJson` 交给前端；macOS 使用 `NSOpenPanel`、Linux 使用 `GtkFileChooser`，通过 `window.__insysEditorHost.openAsset` 复用同一 dirty 确认、recent paths 和只读导入逻辑；外部浏览器模式没有 host bridge，会提示改用路径输入。
- 外部浏览器入口：`File > Open in Browser`（Ctrl+Shift+B）在运行中打开当前 token URL；`--open-browser` 启动时同时打开 WebView2 和外部浏览器；`--browser-only` 跳过 WebView2，只保留本地 server 窗口；`--browser PATH` 指定浏览器可执行文件，默认使用系统默认浏览器。外部浏览器与 WebView2 共用同一 server，关闭 shell 窗口会停止 server。
- 可选 auto-save 对齐 `InputEditorUserSettings.autoSaveInputActionAssets` 和 `InputActionsEditorWindow.OnLostFocus`：默认关闭，偏好持久化到 localStorage；开启时立即保存当前 dirty 资产，窗口失焦时保存；WebView2 关闭时由 shell 检测 `data-auto-save` 直接保存后退出。Raw JSON 未应用、validation error 或无有效保存路径时不自动保存，窗口保留并回退到放弃确认。
- 关闭保护对齐 `Packages/com.unity.inputsystem/InputSystem/Editor/UITKAssetEditor/InputActionsEditorWindow.cs` 的 `hasUnsavedChanges` / `saveChangesMessage`：资产有未保存修改时弹出 Save / Discard / Cancel，Save 通过前端 Save 按钮完成并轮询 dirty 状态，超时或失败保持窗口打开；仅 Raw JSON 未应用时只提供 Discard / Cancel。浏览器模式用 `beforeunload` 做同等提醒。
- 前端在 `.app` 上暴露 `data-dirty` / `data-raw-dirty`，供 shell 查询关闭状态；Playwright 断言 dirty 在编辑、保存和重载后的变化。
- WebView2 构建：`cmake/WebView2.cmake` 固定下载 `Microsoft.Web.WebView2 1.0.3537.50` 并静态链接 loader，`INSYS_WEBVIEW2_ROOT` 可指定离线 SDK，`INSYS_BUILD_EDITOR_SHELL=OFF` 可关闭；`web/` 资源在构建后复制到 shell 输出目录。
- Playwright：`editor/web/e2e` 启动临时 C++ server，覆盖打开/编辑/保存/重开、`.insysb` bake/只读导入、外部修改冲突、Preview 键盘驱动；`webview2.spec.ts` 通过 `--remote-debugging-port` 连接真实 WebView2，验证同一编辑/保存流程。
- 当前测试计数：`insys_editor_tests` 28 cases / 375 assertions；前端 Vitest 24 cases；Playwright 7 cases（含 WebView2）。macOS/Linux 原生壳尚未完成平台编译/smoke，因此 E4 仍未完整验收。
- Windows 便携包：CMake `install` 规则 + CPack ZIP 生成 `insys-editor-0.1.0-windows-x64.zip`，包含 `insys_editor.exe`、`web/`、README 和 MSVC runtime DLL；同时生成 `.sha256`。已从解压目录直接 smoke 通过。
- Windows MSIX：`packaging/msix/AppxManifest.xml.in`、图标资源和 `build-msix.ps1` 使用 Windows SDK `makeappx` 生成 `insys-editor-0.1.0-windows-x64.msix`；`-SelfSign` 生成开发证书并签名，CMake 目标 `insys_editor_msix` 可一键打包。已 unpack 并从包内 exe smoke 通过；自签名包已在导入根证书后完成 `Add-AppxPackage`、包身份启动和 `Remove-AppxPackage` 验证。
- macOS/Linux：`tools/editor_shell/main_macos.mm` 使用 WKWebView，`main_linux.cpp` 使用 WebKitGTK/GTK3；共享 `posix_shell.cpp` 处理 CLI、server、外部浏览器和静态资源路径。前端暴露 `window.__insysEditorHost` 供原生壳执行 open/save/state。当前 Windows 环境无法编译或 smoke 这两个目标，E4 平台验收仍未完成。

## 9. 测试计划

### 9.1 C++ 单测

E0 已落地 `tests/test_editor_session.cpp`、`tests/test_editor_protocol.cpp`、
`tests/test_editor_server.cpp`，覆盖 SHA-256 revision、路径白名单、冲突/备份、
JSON-RPC 错误、token/Origin、静态文件和 WebSocket 往返。

- Editor catalog：
  - layout/control 数量与 `LayoutRegistry` 一致。
  - 关键 path 存在，如 `<Gamepad>/leftStick`、`<Keyboard>/space`。
  - processor/interaction/composite 名称能被工厂创建。
- Validation：
  - 坏 JSON、未知字段、重复名称、重复 GUID。
  - 未知 interaction/processor/composite。
  - composite 缺 part、未知 part、空 part。
  - path 无法匹配、expected control type 不匹配。
  - ControlScheme binding group 不匹配。
- Protocol：
  - 请求/响应/错误/通知编解码。
  - 非法 JSON、超大 payload、未知方法。
  - token 失败、Origin 失败。
- Preview：
  - 空资产、热更新、设备保留。
  - 键盘/鼠标事件注入。
  - HID 多 report 设备只生成一个 Device。
- HID tools：
  - devices snapshot、descriptor 导出、report listener。
  - feature 读写失败返回 `HidError`。
- E4 workflow：
  - `.insysb` bake、binary import、read-only save 拒绝、已有目标 revision 冲突。
  - JSON/binary schema 版本错误和 migration 诊断。
  - 稳定序列化顺序和幂等保存。

### 9.2 前端测试

- Store：
  - 命令 apply/invert。
  - 选择索引修正。
  - undo/redo 边界。
- UI：
  - 新增 ActionMap/Action/Binding 的菜单和快捷键。
  - 重命名、拖拽、复制粘贴。
  - 诊断点击定位到 Inspector/Raw JSON。
  - dirty/保存/冲突状态。
- 控件选择器：
  - 搜索、过滤、Listen、手动 path。
  - derived bindings 展示。
- E4 工作流：
  - `.insysb` 导入、Save As、覆盖确认、bake 导出。
  - schema 版本错误和 migration 提示。
  - 最近文件、面板宽度、预览轮询频率、auto-save 偏好持久化。
  - 1000 bindings 扁平化行数和可视窗口上限。
  - `data-dirty` / `data-raw-dirty` / `data-auto-save` 随编辑、保存、重载和设置变化，关闭时浏览器触发 `beforeunload`。
  - auto-save 开启后 blur 触发保存，关闭前由 shell 自动保存。

### 9.3 集成/E2E

已实现 Playwright（`editor/web/e2e`）：

- `global-setup.ts` 以 `--port 0` 启动临时 `insys_editor_server`，使用临时资产根目录，并把 URL/PID 写入测试状态；teardown 终止进程并清理。
- `editor.spec.ts` 覆盖：打开 fixture、创建 ActionMap/Action/Binding 并保存重开、auto-save blur、`.insysb` bake/只读导入、外部修改冲突、Preview 启动和键盘驱动注入；该套件本身通过 Chromium 外部访问同一 server URL，覆盖外部浏览器协议路径。
- `webview2.spec.ts` 启动真实 `insys_editor.exe`，通过 `--remote-debugging-port` 用 Playwright `connectOverCDP` 连接 WebView2，验证页面加载、编辑、保存，以及 host->web `Open Asset` 消息能切换当前资产。
- 运行：先构建 editor 目标，再依次执行 `cd editor/web`、`npx playwright install chromium`、`npm run test:e2e`。

尚未覆盖：WebSocket 断开重连、Composite/ControlScheme E2E、真实 HID 硬件、macOS/Linux WebView。

### 9.4 人工验收

- Windows WebView2 桌面壳启动、窗口缩放、关闭和浏览器 fallback。
- 原生打开：`File > Open Asset...`/Ctrl+O 弹出文件对话框，选择 JSON 后前端打开并刷新路径。已用 SendKeys 自动化手工验证。
- 外部浏览器：`--open-browser` 启动后 WebView2 与外部浏览器同时加载；`File > Open in Browser` 可再次打开；`--browser-only` 不创建 WebView2；`--browser PATH` 能启动指定浏览器。已用测试 stub 验证 URL/token 传递和 server 生命周期。
- 有未保存修改时关闭窗口：Save 保存后退出，Discard 放弃后退出，Cancel 保持窗口打开；Raw JSON 未应用时只允许 Discard/Cancel。已手工验证 Save、Discard、Cancel 三条路径。
- 开启 auto-save 后关闭窗口：不弹保存确认，修改写入后再退出；已手工验证。
- Windows 便携 ZIP：解压后从包内 `insys_editor.exe` 启动，确认使用包内 `web/` 且 smoke 通过；`.sha256` 可校验。
- Windows MSIX：`makeappx unpack` 后从包内 exe 启动 smoke；检查 `AppxManifest.xml`、图标、`AppxSignature.p7x` 和签名证书；开发根证书导入 `Cert:\LocalMachine\Root`、证书导入 TrustedPeople 后执行 `Add-AppxPackage`、启动和卸载。
- Windows 真实 DS4/DualSense/Switch Pro/Steam Deck。
- 手柄热插拔、重连、同路径重连。
- 捕获摇杆/扳机/按钮，检查 path 和值。
- 发送 rumble/lightbar/adaptive trigger。
- 中文名称、备注、搜索、IME。
- 大资产滚动和编辑流畅度。

## 10. 安全

- 默认只绑定 `127.0.0.1`，不监听公网地址。
- 启动时生成随机 token，前端从页面 URL 的 `?token=` 读取并在 WebSocket handshake 中提交；`session.hello` 返回 `tokenValid`。
- 校验 `Origin`/`Referer`，拒绝非本地页面。
- 文件访问限制在用户配置的资产根目录；拒绝 `..`、设备路径和符号链接逃逸。
- 原始 HID 写入必须由 UI 明确触发，并在界面显示设备、report id 和字节数。
- 不执行任意 shell；`asset_bake` 只调用内部函数，不通过命令行拼接。
- 禁止加载远程脚本；生产构建 CSP 限制为本地资源。
- 日志中不输出 token、完整文件内容或 HID 序列号；设备 id 可脱敏。
- WebView 生产模式关闭 devtools；开发模式显式开启。
- WebView2 壳仅放行 `http://127.0.0.1:<port>/` 导航，阻止新窗口/弹窗；`--remote-debugging-port` 必须显式开启，且只用于本机 E2E。
- WebView2 启动失败时回退系统浏览器；日志只记录脱敏 URL。

## 11. 打包

### 11.1 开发模式

```text
npm run dev          # Vite，默认 5173
insys_editor_server  # 本地服务，默认 127.0.0.1:17800
```

Vite 代理 `/ws` 到 C++ 服务，避免 CORS；前端打开时需要把 server 打印的
`?token=...` 加到 Vite URL。

### 11.2 生产模式

```powershell
cd editor/web
npm install
npm run build

cd ../..
cmake --preset editor
cmake --build --preset editor-debug --target insys_editor
.\build-editor\Debug\insys_editor.exe --asset-root .\samples\acceptance\assets
```

- `insys_editor.exe` 内嵌 editor server，使用随机本地端口和 token，不需要手动打开浏览器。
- `insys_editor_server.exe` 仍可单独运行，用于开发/E2E；shell 同时构建。
- 构建后 `editor/web/dist` 复制到 `build-editor\Debug\web`，shell 优先加载 exe 同目录的 `web`。
- Windows WebView2 SDK 由 `cmake/WebView2.cmake` 下载并静态链接；离线环境设置 `INSYS_WEBVIEW2_ROOT` 指向解压后的 `Microsoft.Web.WebView2` 包，或用 `INSYS_BUILD_EDITOR_SHELL=OFF` 关闭。
- 运行机器需要 Edge WebView2 Runtime；缺失时 shell 打开外部浏览器并显示本地服务地址。
- 原生打开：`File > Open Asset...`（Ctrl+O）选择 JSON/`.insysb`；外部浏览器模式使用路径输入。
- 外部浏览器：`File > Open in Browser`（Ctrl+Shift+B）打开当前 URL；`--open-browser` 在启动时同时打开外部浏览器；`--browser-only` 只打开外部浏览器；`--browser PATH` 指定浏览器可执行文件。外部 URL 含 session token，不要分享。
- 工具栏 `Auto save` 默认关闭；开启后失焦自动保存，WebView2 关闭时 shell 自动保存 dirty 资产。Raw JSON 未应用或 validation 错误时不会自动保存，仍会提示。
- 关闭窗口时若资产有未保存修改，shell 提供 Save / Discard / Cancel；浏览器模式使用 `beforeunload` 提醒。外部浏览器模式没有 WebView2 脏状态可查询，关闭 shell 会直接停止 server。
- macOS WKWebView、Linux WebKitGTK 桌面壳代码已实现，但尚未在当前 Windows 环境编译或 smoke；需要平台工具链完成验收。macOS 包未签名/公证，Linux 暂只提供可执行文件 + web 资源，没有 deb/AppImage。
- Playwright：`npx playwright install chromium` 后运行 `npm run test:e2e`；Windows 上额外运行真实 WebView2 CDP 用例。

```powershell
# WebView2 + Chrome
.\build-editor\Debug\insys_editor.exe --asset-root .\samples\acceptance\assets `
  --open-browser --browser "C:\Program Files\Google\Chrome\Application\chrome.exe"

# 只用外部浏览器
.\build-editor\Debug\insys_editor.exe --asset-root .\samples\acceptance\assets `
  --browser-only --browser "C:\Program Files\Google\Chrome\Application\chrome.exe"
```

### 11.3 Windows 便携包

```powershell
cmake --build --preset editor-release --target insys_editor
cpack --config build-editor\CPackConfig.cmake -C Release
```

- 输出：`build-editor\package\insys-editor-0.1.0-windows-x64.zip` 和 `.sha256`。
- 包内含 `insys_editor.exe`、`web/`、`README.md`、MSVC runtime DLL；`insys_editor` 优先加载 exe 同目录 `web/`。
- 目标机器仍需 Edge WebView2 Runtime；不需要 Visual Studio 或 vcpkg。
- 当前是便携 ZIP；另有 MSIX 包，见 11.4。

### 11.4 Windows MSIX

```powershell
cmake --build --preset editor-release --target insys_editor_msix

# 本地可安装的开发签名包
cmake --preset editor -DINSYS_MSIX_SELF_SIGN=ON
cmake --build --preset editor-release --target insys_editor_msix
```

- 输出：`build-editor\package\insys-editor-0.1.0-windows-x64.msix` 和 `.sha256`。
- 包内含 `insys_editor.exe`、`web/`、Assets、MSVC runtime；`EntryPoint` 为 `Windows.FullTrustApplication`，声明 `runFullTrust`。
- 默认不签名；`-SelfSign` 会生成 `.pfx`/`.cer` 并签名，证书 Subject 必须与 manifest Publisher 一致。
- 自签名包安装时必须把根证书加入 `Cert:\LocalMachine\Root`（管理员），并把证书加入 `TrustedPeople`；只导入 TrustedPeople 会报 `0x800B0109`。
- 已实测：`Add-AppxPackage` 成功、通过 `shell:AppsFolder\...!insys.editor` 启动、`Remove-AppxPackage` 卸载成功；测试完成后已移除证书。
- 生产签名可通过 `-DINSYS_MSIX_PUBLISHER`、`-DINSYS_MSIX_CERTIFICATE`、`-DINSYS_MSIX_TIMESTAMP_URL` 配置；PFX 密码读取 `INSYS_MSIX_CERTIFICATE_PASSWORD` 环境变量。
- 目标机器仍需 Edge WebView2 Runtime。
- 尚未配置 Microsoft Store 身份。

### 11.5 macOS/Linux 原生壳

```bash
cd editor/web
npm install
npm run build
cd ../..

cmake --preset editor-unix
cmake --build --preset editor-unix-debug
```

- macOS：使用系统 WKWebView/Cocoa，产物为 `build-editor-unix/insys_editor.app`。
- Linux：使用 GTK 3 + WebKitGTK 4.1（找不到时回退 4.0），产物为 `build-editor-unix/insys_editor`；需要 `pkg-config` 和对应的开发包。
- 两个 shell 都支持 `--browser-only`、`--open-browser`、`--browser`、原生 Open Asset 菜单和关闭保护；共享 CLI/server/外部浏览器代码在 `posix_shell.cpp`。
- 前端通过 `window.__insysEditorHost` 暴露 `openAsset`、`state`、`save`，供非 WebView2 壳调用。
- 当前 Windows 工作区没有 macOS/Linux 工具链，以上目标尚未实际编译和 smoke；E4 平台验收仍未完成。

### 11.6 版本握手

`session.hello` 返回：

```json
{
  "protocolVersion": 1,
  "serverVersion": "0.2.0",
  "capabilities": ["preview", "capture", "hid", "bake"]
}
```

前端不支持协议版本时拒绝连接并提示升级。

## 12. Unity 原实现对照

| Unity 源码 | 作用 | 本编辑器对应 |
|---|---|---|
| `Editor/UITKAssetEditor/InputActionsEditorWindow.cs` | 编辑器窗口、dirty、autosave、保存、快捷键 | `EditorSession` + 前端工具栏 |
| `Editor/UITKAssetEditor/InputActionsEditorState.cs` | 选择状态、序列化状态容器 | `EditorDocument.selection` + Zustand |
| `Editor/UITKAssetEditor/Commands/Commands.cs` | 增删改、复制、移动、保存 | 前端命令模式 + 后端保存 |
| `Editor/UITKAssetEditor/Views/ActionMapsView.cs` | ActionMap 列表、重命名、拖拽 | 左栏 ActionMap 树 |
| `Editor/UITKAssetEditor/Views/ActionsTreeView.cs` | Action/Binding 树、右键菜单、复制粘贴 | 中央 Action/Binding 树 |
| `Editor/UITKAssetEditor/Views/ActionPropertiesView.cs` | Action type/control type/priority/initial check | Action Inspector |
| `Editor/UITKAssetEditor/Views/BindingPropertiesView.cs` | path、control scheme、derived bindings | Binding Inspector |
| `Editor/UITKAssetEditor/Views/CompositeBindingPropertiesView.cs` | Composite 类型和参数 | Composite Inspector |
| `Editor/UITKAssetEditor/Views/ControlSchemesView.cs` | ControlScheme 弹窗、设备要求、optional | ControlScheme 编辑器 |
| `Editor/ControlPicker/InputControlPickerDropdown.cs` | 设备/控件树、usage、搜索、Listen | 控件选择器 |
| `Editor/ControlPicker/InputControlPathEditor.cs` | path 输入、expected layout、过滤 | Path 输入 + 校验 |
| `Editor/AssetEditor/InputActionAssetManager.cs` | 编辑副本、保存资产 | `FileService` + revision |
| `Editor/AssetImporter/InputActionImporter.cs` | JSON 资产导入 | `asset.open` / `.insysb` 导入 |
| `Editor/EditorInputControlLayoutCache.cs` | layout/control 缓存 | `CatalogService` |
| `Editor/AssetEditor/MatchingControlPaths.cs` | derived bindings | `catalog.controlPaths` |
| `Editor/UITKAssetEditor/Views/NameAndParametersListView.cs` | interaction/processor 参数 | 参数列表编辑器 |
| `Editor/UITKAssetEditor/Views/CopyPasteHelper.cs` | 复制粘贴格式 | 前端剪贴板命令 |

对齐原则：

- 保留 Unity 的“ActionMap → Action → Binding/Composite”层级。
- 保留选择状态、重命名、复制、拖拽、右键菜单和快捷键行为。
- 不复制 UIElements 的 VisualElement/UXML。
- 不依赖 Unity SerializedObject；直接操作 JSON AST。

## 13. C++ 复用与改造清单

| 现有代码 | 用途 | 需要的改造 |
|---|---|---|
| `include/insys/actions/action_asset.h` | 资产模型 | 增加编辑器所需的只读访问/查询 |
| `include/insys/actions/action_map.h` | Map/Action/Binding | 增加删除/移动辅助或在前端命令层处理 |
| `include/insys/actions/binding.h` | Binding 字段 | 确认 JSON 字段覆盖 |
| `include/insys/actions/control_scheme.h` | ControlScheme | 首版只支持 scheme 级 optional |
| `include/insys/asset/asset_loader.h` | 加载/保存/烘焙 | E0 已支持 `priority` JSON；后续增加 override 字段与编辑器诊断封装 |
| `include/insys/layout_registry.h` | layout 枚举 | 供 CatalogService 使用 |
| `include/insys/device_builder.h` | 构建控件树 | 供 CatalogService 生成 path |
| `include/insys/control_path.h` | path 解析/匹配 | path 校验 |
| `include/insys/debugger_model.h` | 运行时快照 | 预览页直接复用 |
| `include/insys/backend/runtime.h` | 无窗口 Runtime | Editor preview |
| `include/insys/backend/hid_backend.h` | HID 设备/命令 | 增加 devices/report listener |
| `include/insys/backend/windows_hid.h` | Windows HID | 抽到 `insys_hid_platform` |
| `include/insys/backend/linux_hid.h` | Linux HID | 抽到 `insys_hid_platform` |
| `include/insys/backend/macos_hid.h` | macOS HID | 抽到 `insys_hid_platform` |
| `src/backend/glfw/glfw_backend.cpp` | Demo 输入 | E0 已改为链接 `insys_hid_platform` |
| `include/insys/editor/editor_session.h` | 文件打开/校验/保存 | E0 已实现 SHA-256 revision、路径白名单和原子保存 |
| `include/insys/editor/editor_protocol.h` | JSON-RPC 方法分发 | E0 已实现基础错误码和请求大小限制 |
| `include/insys/editor/editor_server.h` | 本地 HTTP/WebSocket 服务 | E0 已实现 token、Origin、静态文件和 WS |
| `tools/asset_bake` | JSON→insysb | 编辑器保存/烘焙调用同一实现 |

## 14. 风险与默认决策

### 风险

1. **JSON 序列化不完整**
   - E0 已补齐 `priority`；override 字段仍然只有二进制支持。
   - 如果编辑器后续直接编辑 override，JSON 保存仍可能丢失运行时数据。
   - 默认决策：override 首版不编辑并在 UI 标注。

2. **严格未知字段**
   - 当前 loader 拒绝未知字段，未来版本资产可能无法打开。
   - 默认决策：首版严格模式；后续加 schema 版本和保留未知字段。

3. **平台 HID 与 GLFW 绑定**
   - 不抽出 `insys_hid_platform`，编辑器会被迫链接 GLFW/Demo。
   - 默认决策：E0 必须完成抽取。

4. **浏览器输入与本地手柄不一致**
   - 键盘/鼠标来自 Web 事件，手柄来自本地 HID。
   - 默认决策：键盘/鼠标只用于 path 捕获和预览注入；手柄捕获走 HID。

5. **ControlScheme 能力差异**
   - C++ 当前没有每设备 optional 和完整 DeviceRequirement。
   - 默认决策：首版只支持方案级 optional，文档明确限制。

6. **大资产性能**
   - 1000+ Binding 时树和 JSON 同步可能卡顿。
   - 默认决策：虚拟列表、150 ms preview debounce、增量诊断。

### 待确认但可先按默认实现

- `.insysb` 是否需要可视化编辑：默认只读导入/导出。
- 是否需要编辑 `priority`：E0 已补齐 JSON 并开放；后续只调整 UI 呈现和范围。
- 是否需要每设备 optional：默认不做，需 core 数据模型扩展。
- 桌面壳是否用 WebView：默认先浏览器开发，后续 WebView2/WKWebView/WebKitGTK。

## 15. 完成定义

M8 完成需要同时满足：

1. E0–E4 任务全部完成。
2. `insys_tests`、UI tests、编辑器 C++ tests 全绿。
3. 前端 typecheck、unit tests、Playwright smoke 通过。
4. 打开→编辑→保存→重新加载资产行为一致。
5. 真实手柄捕获、预览、HID 工具在 Windows 实机通过。
6. Linux/macOS 至少完成编译验证；macOS IOHID、Linux hidraw 运行验证单独记录。
7. 编辑器不链接 GLFW/RmlUi，运行时不修改资产文件之外的系统状态。
8. 安全要求全部满足，尤其是 token、Origin、路径白名单和 HID 写入确认。
9. 文档记录能力、限制、协议版本和迁移规则。
