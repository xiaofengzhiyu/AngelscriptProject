# AngelScript 调试适配器计划：DAP Debug Adapter + VS Code 扩展

## 背景与目标

### 现有调试基础设施

当前插件已经具备完整的调试服务器端实现（`FAngelscriptDebugServer`），运行在 UE 进程内：

- **传输层**：TCP 监听，默认端口 27099（可通过 `-asdebugport=` 覆盖）
- **协议格式**：自定义二进制协议，消息结构为 `[4字节长度][1字节消息类型][FArchive序列化体]`
- **消息类型**：约 30 种，覆盖完整调试工作流
  - 会话控制：`StartDebugging`、`StopDebugging`、`Disconnect`、`PingAlive`、`DebugServerVersion`
  - 执行控制：`Pause`、`Continue`、`StepOver`、`StepIn`、`StepOut`
  - 断点管理：`ClearBreakpoints`、`SetBreakpoint`、`SetDataBreakpoints`、`ClearDataBreakpoints`
  - 状态检查：`RequestCallStack`/`CallStack`、`RequestVariables`/`Variables`、`RequestEvaluate`/`Evaluate`
  - 代码信息：`Diagnostics`、`DebugDatabase`/`DebugDatabaseFinished`/`DebugDatabaseSettings`、`GoToDefinition`
  - 资产管理：`AssetDatabaseInit`/`AssetDatabase`/`AssetDatabaseFinished`、`FindAssets`、`CreateBlueprint`
  - 中断过滤：`BreakOptions`、`RequestBreakFilters`/`BreakFilters`
  - 其他：`EngineBreak`、`ReplaceAssetDefinition`
- **版本协商**：客户端发送 `StartDebugging` 携带 `DebugAdapterVersion`，服务器回复 `DebugServerVersion`（当前 `DEBUG_SERVER_VERSION = 2`）

### 缺失环节

当前项目 **没有任何调试客户端** — 调试服务器在 UE 内运行，但没有程序能连接它。Hazelight 基线通过 VS Code 扩展实现了 IDE 侧的调试客户端，当前插件在这条线上为零。

### 目标

创建一个 **DAP (Debug Adapter Protocol) 调试适配器** 和配套的 **VS Code/Cursor 扩展**，使开发者可以直接在 IDE 中调试 AngelScript 脚本：

1. **DAP 适配器**：独立可执行程序，一端通过 stdin/stdout 实现标准 DAP 协议（与 IDE 通信），另一端通过 TCP 连接现有 AS 调试服务器
2. **VS Code 扩展**：提供 `launch.json` 配置、源码映射、语言关联，使调试体验开箱即用
3. **零侵入**：不修改现有 `FAngelscriptDebugServer` 实现，纯客户端方案

## 技术路线选择

### 方案对比

| 方案 | 优势 | 劣势 | 适用场景 |
|------|------|------|---------|
| **A. TypeScript DAP 适配器 + VS Code 扩展（推荐）** | 标准 DAP 生态，`@vscode/debugadapter` 框架成熟；VS Code / Cursor 原生支持；社区模式最成熟 | 需要 Node.js 运行时；序列化需要手写 AS 协议的 JS 对应 | IDE 集成调试，最广泛的编辑器兼容 |
| **B. C++ 独立 DAP 适配器** | 可直接复用 `AngelscriptDebugServer.h` 中的消息类型定义；无 Node.js 依赖 | C++ 实现 JSON-RPC 较繁琐；独立编译分发维护成本高 | 如果需要嵌入 UE 构建链或特殊性能场景 |
| **C. Python DAP 适配器** | 开发最快；`debugpy` 参考丰富 | 多一层运行时依赖；AS 二进制协议在 Python 中处理不如 TS 直观 | 快速原型 |
| **D. 改造 DebugServer 直接输出 DAP** | 最简单的集成路径 | 破坏现有协议兼容性；侵入 UE 运行时 | 不推荐 |

### 选定方案：A — TypeScript DAP 适配器 + VS Code 扩展

理由：

- DAP 是微软定义的标准调试协议，VS Code / Cursor / Visual Studio 等都原生支持
- `@vscode/debugadapter` 和 `@vscode/debugprotocol` npm 包提供了完整的 DAP 框架，处理 JSON-RPC 传输、请求-响应匹配等底层逻辑
- Hazelight 基线也采用了 VS Code 扩展方案，验证了这条技术路线的可行性
- 调试适配器可作为 VS Code 扩展的内嵌组件，无需独立分发

## 范围与边界

### 在范围内

- DAP 调试适配器：连接 AS 调试服务器，翻译 DAP ↔ AS 自定义协议
- VS Code 扩展：`launch.json` 配置 schema、AngelScript 语言关联、调试 UI 集成
- 核心调试能力：断点设置/清除、单步执行（Step In/Over/Out）、暂停/继续、调用栈查看、变量查看、表达式求值
- 诊断信息：编译错误/警告实时推送到 IDE Problems 面板
- 源码导航：GoToDefinition 支持
- 数据断点：对变量地址的硬件写断点支持

### 不在范围内

- 修改现有 `FAngelscriptDebugServer` 实现
- LSP (Language Server Protocol) 完整实现（可后续单独立项）
- 远程调试（非 localhost）的安全认证
- AngelScript 语法高亮/自动补全（可后续通过 TextMate grammar 或 LSP 扩展）
- 代码覆盖率 UI 集成（CodeCoverage 已有 HTML 报告，IDE 集成为后续增量）

## AS 自定义协议规格整理

### 传输帧格式

```
┌──────────────────┬──────────────────┬──────────────────────────┐
│  MessageLength   │  MessageType     │  Payload (FArchive)      │
│  (int32, 4bytes) │  (uint8, 1byte)  │  (variable length)       │
└──────────────────┴──────────────────┴──────────────────────────┘
```

- `MessageLength` 是 Payload 长度（包含 MessageType 那 1 字节）
- 字节序：小端序（Little Endian，FArchive 默认）
- FString 序列化格式：`[int32 长度][UTF-16LE 字节][null终止符]`（UE 的 FArchive FString 格式）
- TArray 序列化格式：`[int32 元素个数][逐元素序列化]`
- bool 序列化格式：`[uint32, 0或1]`（FArchive 将 bool 按 4 字节序列化）

### 消息类型与 DAP 映射

| AS 消息类型 | 方向 | DAP 对应 | 说明 |
|------------|------|---------|------|
| `StartDebugging` | C→S | `attach` request | 携带 `DebugAdapterVersion` (int32) |
| `DebugServerVersion` | S→C | `attach` response 的一部分 | 回复 `DebugServerVersion` (int32) |
| `StopDebugging` | C→S | `disconnect` request | |
| `Disconnect` | C→S | 连接关闭 | |
| `Pause` | C→S | `pause` request | |
| `Continue` | C→S | `continue` request | |
| `StepIn` | C→S | `stepIn` request | |
| `StepOver` | C→S | `next` request | |
| `StepOut` | C→S | `stepOut` request | |
| `HasStopped` | S→C | `stopped` event | 携带 Reason/Description/Text |
| `HasContinued` | S→C | `continued` event | |
| `ClearBreakpoints` | C→S | `setBreakpoints` 的一部分 | 按文件清除 |
| `SetBreakpoint` | C→S/S→C | `setBreakpoints` 的一部分 | S→C 方向携带已验证断点行号 |
| `SetDataBreakpoints` | C→S | `setDataBreakpoints` request | |
| `ClearDataBreakpoints` | C→S | `setDataBreakpoints`（空列表时） | |
| `RequestCallStack` | C→S | `stackTrace` request | |
| `CallStack` | S→C | `stackTrace` response | |
| `RequestVariables` | C→S | `variables` / `scopes` request | Path 格式 `{Frame}:Variable.Member` |
| `Variables` | S→C | `variables` / `scopes` response | |
| `RequestEvaluate` | C→S | `evaluate` request | |
| `Evaluate` | S→C | `evaluate` response | |
| `GoToDefinition` | C→S | 非标准 DAP；需通过自定义 request 或 redirect to LSP | |
| `Diagnostics` | S→C | 非标准 DAP；可通过自定义 event 推送到 Problems 面板 | |
| `RequestDebugDatabase` | C→S | 初始化时主动请求 | |
| `DebugDatabase`/`DebugDatabaseFinished` | S→C | 自定义 event；脚本符号信息 | |
| `DebugDatabaseSettings` | S→C | 自定义 event；AS 引擎配置 | |
| `AssetDatabaseInit`/`AssetDatabase`/`AssetDatabaseFinished` | S→C | 自定义 event | |
| `BreakOptions` | C→S | `setExceptionBreakpoints` request | |
| `RequestBreakFilters`/`BreakFilters` | C→S / S→C | `exceptionBreakpointsFilter` capability | |
| `PingAlive` | S→C | 心跳，适配器内部处理 | |
| `EngineBreak` | C→S | 自定义 request | 触发 C++ 断点 |
| `CreateBlueprint` | C→S | 自定义 request | |
| `ReplaceAssetDefinition` | C→S | 自定义 request | |
| `FindAssets` | S→C | 自定义 event | |

## 分阶段执行计划

### Phase 1：项目骨架搭建

> 目标：创建 VS Code 扩展和 DAP 适配器的项目结构，验证从 IDE 启动调试会话到 TCP 连接 AS 调试服务器的基本链路。

- [ ] **P1.1** 创建扩展项目结构
  - 在 `Plugins/Angelscript/Tools/AngelscriptDebugAdapter/` 下初始化 VS Code 扩展项目。这是调试适配器和 VS Code 扩展的统一代码库，扩展内嵌适配器（inline debug adapter 模式），不需要独立分发可执行文件
  - 包含 `package.json`（定义 `contributes.debuggers`、`contributes.breakpoints`、`contributes.languages`）、`tsconfig.json`、`.vscodeignore`
  - 扩展 `activationEvents` 配置为 `onDebugResolve:angelscript`
  - `launch.json` 配置 schema 支持：`host`（默认 `127.0.0.1`）、`port`（默认 `27099`）、`request: attach`
  - 不需要 `launch` 请求类型（UE 进程由用户手动启动），只需 `attach`
- [ ] **P1.1** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: scaffold VS Code extension and DAP adapter project`

- [ ] **P1.2** 实现 AS 自定义协议的 TypeScript 编解码器
  - 核心挑战是精确还原 UE `FArchive` 的序列化格式：小端序 int32、uint8、uint64、bool (4字节)、FString (int32长度 + UTF-16LE + null)、TArray (int32计数 + 逐元素)
  - 创建 `src/asProtocol.ts`：定义所有 `EDebugMessageType` 枚举值、每种消息的 TypeScript 类型、编码/解码函数
  - 创建 `src/asArchive.ts`：实现 `FArchiveReader` / `FArchiveWriter` 工具类，模拟 UE FArchive 的 `operator<<` 行为
  - 编写单元测试验证序列化正确性（特别是 FString 的 UTF-16LE 处理和 TArray 嵌套场景）
- [ ] **P1.2** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: AS custom protocol TypeScript codec`

- [ ] **P1.3** 实现基础 TCP 客户端和 DAP 会话管理
  - 创建 `src/asDebugClient.ts`：封装 TCP 连接管理（connect/disconnect/reconnect）、消息帧的拆包与组包、消息队列与超时处理
  - 需要处理 TCP 流式拆包：接收端可能一次收到多条消息或半条消息，需要按 `MessageLength` 头缓冲和裁剪
  - 创建 `src/angelscriptDebugSession.ts`：继承 `@vscode/debugadapter` 的 `DebugSession`，实现 `attachRequest` → 建立 TCP 连接 → 发送 `StartDebugging` → 等待 `DebugServerVersion` 回复
  - 实现 `disconnectRequest` → 发送 `StopDebugging` → 关闭 TCP 连接
  - 验证：在 UE 编辑器运行状态下，从 VS Code 发起 Attach 并成功建立调试会话
- [ ] **P1.3** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: TCP client and DAP session lifecycle`

### Phase 2：核心调试能力

> 目标：实现断点、执行控制、调用栈、变量查看等核心调试功能，使基础调试工作流可用。

- [ ] **P2.1** 实现断点管理
  - DAP 的 `setBreakpoints` 请求按文件发送完整断点列表；需要翻译为 AS 协议的 `ClearBreakpoints`（按文件清除旧断点）+ 多次 `SetBreakpoint`（逐条设置）
  - 处理 AS 服务器的断点验证回复：服务器可能调整断点行号（对齐到最近的代码行），适配器需要将调整后的行号通过 DAP `Breakpoint` 事件回传给 IDE
  - 处理断点 ID 映射：DAP 使用整数 ID，AS 协议也使用整数 ID，需要维护映射关系
  - `package.json` 中注册 `breakpoints` 对 `angelscript` 语言的支持
- [ ] **P2.1** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: breakpoint management (set/clear/verify)`

- [ ] **P2.2** 实现执行控制（暂停、继续、单步）
  - `pause` → 发送 AS `Pause` 消息
  - `continue` → 发送 AS `Continue` 消息
  - `stepIn` / `next` / `stepOut` → 发送 AS `StepIn` / `StepOver` / `StepOut` 消息
  - 监听 AS 服务器的 `HasStopped` 事件 → 翻译为 DAP `stopped` 事件（携带 reason：`breakpoint` / `step` / `pause` / `exception`，从 `FStoppedMessage.Reason` 字段映射）
  - 监听 AS 服务器的 `HasContinued` 事件 → 翻译为 DAP `continued` 事件
  - `HasStopped` 的 `Description` 和 `Text` 字段应映射到 DAP `stopped` 事件的 `description` 和 `text`
- [ ] **P2.2** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: execution control (pause/continue/step)`

- [ ] **P2.3** 实现调用栈查看
  - DAP `stackTrace` request → 发送 AS `RequestCallStack` → 等待 AS `CallStack` 回复
  - 将 `FAngelscriptCallFrame` 翻译为 DAP `StackFrame`：`Name` → `name`，`Source` → 构造 `Source` 对象（需要将 AS 脚本路径映射到本地文件系统路径），`LineNumber` → `line`
  - `FAngelscriptCallFrame.ModuleName` 可选字段用于辅助源码定位
  - 关键路径映射：AS 调试服务器使用的脚本文件路径可能是 UE 内部路径格式，需要翻译为本地文件系统绝对路径。初步方案是让 `launch.json` 配置 `scriptRoot` 参数指定脚本根目录
- [ ] **P2.3** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: call stack viewing with source mapping`

- [ ] **P2.4** 实现变量查看和作用域
  - DAP `scopes` request → 为当前栈帧生成作用域列表（Locals、This 等）
  - DAP `variables` request → 发送 AS `RequestVariables`（Path 格式 `{Frame}:` 为顶层作用域，`{Frame}:Variable.Member` 为展开子成员）→ 等待 AS `Variables` 回复
  - 将 `FAngelscriptVariable` 翻译为 DAP `Variable`：`Name` → `name`，`Value` → `value`，`Type` → `type`，`bHasMembers` → 决定 `variablesReference` 是否非零（非零时 IDE 会发送子变量请求）
  - 需要维护 `variablesReference` 到 AS 路径的映射表，每次停止时重置
  - `ValueAddress` 和 `ValueSize` 字段在 `DebugAdapterVersion >= 2` 时可用，用于数据断点功能
- [ ] **P2.4** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: variable inspection and scope browsing`

- [ ] **P2.5** 实现表达式求值
  - DAP `evaluate` request → 发送 AS `RequestEvaluate`（携带表达式路径和当前栈帧）→ 等待 AS `Evaluate` 回复
  - 支持 Watch 面板、Debug Console 和 Hover 三个求值上下文
  - `evaluate` 的 `context` 参数（`watch` / `repl` / `hover`）目前统一走 AS `RequestEvaluate`，不需要区分处理
- [ ] **P2.5** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: expression evaluation (watch/hover/console)`

### Phase 3：增强功能

> 目标：补齐诊断信息、数据断点、异常断点过滤等进阶功能，接近 Hazelight 的完整调试工作流体验。

- [ ] **P3.1** 实现诊断信息推送
  - AS 调试服务器在脚本编译后主动向所有客户端推送 `Diagnostics` 消息（按文件推送错误/警告/信息列表）
  - 适配器收到 `Diagnostics` 后通过 VS Code 的 `DiagnosticCollection` API 更新 Problems 面板
  - 这不是标准 DAP 功能，需要在 VS Code 扩展侧（`extension.ts`）订阅适配器发出的自定义事件并调用 VS Code API
  - 每次收到新的 `Diagnostics` 时按文件替换（而非追加），与 AS 服务器的行为一致
- [ ] **P3.1** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: diagnostics push to Problems panel`

- [ ] **P3.2** 实现异常断点过滤
  - DAP `initialize` response 中声明 `exceptionBreakpointFilters` capability
  - DAP `setExceptionBreakpoints` request → 翻译为 AS `BreakOptions` 消息
  - 在 `attach` 成功后发送 AS `RequestBreakFilters` → 收到 AS `BreakFilters` → 动态更新 IDE 中可用的异常断点过滤器列表
  - `BreakFilters` 携带 `Filters`（过滤器 ID）和 `FilterTitles`（显示名称）对
- [ ] **P3.2** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: exception breakpoint filters`

- [ ] **P3.3** 实现数据断点
  - DAP `dataBreakpointInfo` request → 从当前变量的 `ValueAddress` 和 `ValueSize` 构造数据断点描述
  - DAP `setDataBreakpoints` request → 翻译为 AS `SetDataBreakpoints` 消息（携带地址、大小、命中计数）
  - 当前 AS 调试服务器限制硬件数据断点最多 4 个（`DATA_BREAKPOINT_HARDWARE_LIMIT`），需要在适配器层面向用户报告此限制
  - 仅 Windows 平台支持（基于 x86 Debug Registers Dr0-Dr3）
- [ ] **P3.3** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: data breakpoints (Windows only)`

- [ ] **P3.4** 实现 DebugDatabase 与 AssetDatabase 支持
  - 在 attach 成功后发送 `RequestDebugDatabase` → 接收并缓存 `DebugDatabase`（脚本符号信息 JSON）、`DebugDatabaseSettings`（AS 引擎配置）、`DebugDatabaseFinished`
  - 接收 `AssetDatabaseInit` / `AssetDatabase` / `AssetDatabaseFinished` → 缓存资产列表
  - DebugDatabase 为后续的自动补全、符号跳转等 LSP 功能提供数据基础
  - 初期仅缓存，不暴露 UI；后续可用于 QuickPick 命令
- [ ] **P3.4** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: debug and asset database caching`

- [ ] **P3.5** 实现 GoToDefinition 和 CreateBlueprint 命令
  - 注册 VS Code 命令 `angelscript.goToDefinition` → 发送 AS `GoToDefinition`（TypeName + SymbolName）
  - 注册 VS Code 命令 `angelscript.createBlueprint` → 发送 AS `CreateBlueprint`（ClassName）
  - 这些是自定义扩展命令，不属于标准 DAP 流程
  - GoToDefinition 最终应整合到 LSP 的 `textDocument/definition` 中，但初期作为独立命令提供
- [ ] **P3.5** 📦 Git 提交：`[Plugin/DebugAdapter] Feat: GoToDefinition and CreateBlueprint commands`

### Phase 4：工程化与发布

> 目标：完善文档、测试、打包，使扩展可稳定使用并支持后续维护。

- [ ] **P4.1** 编写单元测试与集成测试
  - 单元测试：AS 协议编解码正确性（覆盖所有消息类型的 round-trip 序列化）
  - 单元测试：DAP ↔ AS 消息翻译正确性
  - 集成测试：mock TCP 服务器模拟 AS 调试服务器，验证完整调试会话流程
  - 测试框架使用 `mocha` + `@vscode/test-electron`（VS Code 扩展标准测试方案）
- [ ] **P4.1** 📦 Git 提交：`[Plugin/DebugAdapter] Test: unit and integration tests`

- [ ] **P4.2** 编写用户文档和快速开始指南
  - `README.md`：扩展功能说明、安装步骤、配置指南
  - `launch.json` 模板和示例配置
  - 常见问题排查（连接失败、断点不触发、路径映射错误等）
  - 更新 `Documents/Guides/` 中的开发者工作流文档
- [ ] **P4.2** 📦 Git 提交：`[Plugin/DebugAdapter] Docs: user documentation and quickstart guide`

- [ ] **P4.3** 扩展打包与分发配置
  - 配置 `vsce` 打包为 `.vsix` 文件
  - `.vscodeignore` 排除源码和测试文件
  - `package.json` 完善：`publisher`、`repository`、`icon`、`categories`、`keywords`
  - 考虑是否发布到 VS Code Marketplace 或仅作为项目内本地安装
- [ ] **P4.3** 📦 Git 提交：`[Plugin/DebugAdapter] Build: VSIX packaging configuration`

## 项目结构

```
Plugins/Angelscript/Tools/AngelscriptDebugAdapter/
├── package.json                  # VS Code 扩展清单
├── tsconfig.json                 # TypeScript 配置
├── .vscodeignore                 # 打包排除
├── src/
│   ├── extension.ts              # 扩展入口（激活/停用、命令注册、诊断管理）
│   ├── angelscriptDebugSession.ts # DAP DebugSession 实现
│   ├── asDebugClient.ts          # TCP 客户端（连接 AS 调试服务器）
│   ├── asProtocol.ts             # AS 消息类型定义与 DAP 映射
│   ├── asArchive.ts              # FArchive 二进制序列化/反序列化
│   └── pathMapping.ts            # 脚本路径 ↔ 本地文件系统路径映射
├── test/
│   ├── asArchive.test.ts         # 序列化单元测试
│   ├── asProtocol.test.ts        # 协议编解码测试
│   └── debugSession.test.ts      # 集成测试
└── launch-template.json          # 用户 launch.json 模板
```

## 验收标准

1. **连接**：从 VS Code/Cursor 发起 Attach，成功连接到 UE 内运行的 AS 调试服务器
2. **断点**：在 `.as` 文件中设置断点，脚本执行到断点时 IDE 正确暂停并定位到源码行
3. **单步执行**：Step In / Step Over / Step Out 正确工作，IDE 实时跟踪当前执行位置
4. **变量查看**：暂停时可展开查看局部变量、this 指针、成员变量
5. **表达式求值**：Watch 面板和 Hover 可求值 AS 表达式
6. **调用栈**：暂停时正确显示 AS 脚本调用栈
7. **诊断**：脚本编译错误/警告实时显示在 Problems 面板
8. **心跳**：适配器正确响应 `PingAlive`，连接断开时优雅清理
9. **零侵入**：不修改现有 `FAngelscriptDebugServer` 任何代码

## 风险与注意事项

### 风险 1：FArchive 序列化格式精确还原

AS 调试协议使用 UE 的 `FArchive` 二进制序列化。TypeScript 侧必须精确还原以下格式：
- `int32`：4 字节小端有符号整数
- `uint8`：1 字节无符号
- `uint64`：8 字节小端无符号（JavaScript 的 `BigInt` 或 `Buffer` 手动处理）
- `bool`：UE FArchive 序列化为 4 字节（`uint32`，0 或 1），不是 1 字节
- `FString`：`[int32 字符数（含 null）][UTF-16LE 字节]`；空字符串编码为 `[int32: 0]`
- `TOptional<FString>`：`FAngelscriptCallFrame.ModuleName` 使用 `IsSet()` 判断，序列化时只有在 Set 状态才写入

缓解：Phase 1 的协议编解码器需要针对每种数据类型编写 round-trip 测试，并用 UE 侧对照验证。

### 风险 2：脚本路径映射

AS 调试服务器使用的脚本文件路径可能是 UE 内部格式（如 `/Script/ModuleName/FileName`），需要映射到开发者本地文件系统路径。`FAngelscriptDebugServer::CanonizeFilename` 有一套路径规范化逻辑，适配器侧需要实现对等的反向映射。

缓解：`launch.json` 中配置 `scriptRoot` 参数；`DebugDatabase` 中可能包含足够的路径信息用于自动推断映射。

### 风险 3：异步消息处理时序

AS 调试协议不是严格的 request-response 模式 — 服务器可以随时推送 `Diagnostics`、`HasStopped`、`AssetDatabase` 等事件。DAP 适配器需要正确处理：
- 在等待某个 response 时收到了不相关的 event
- 服务器推送大量 DebugDatabase 数据时的背压处理
- `PingAlive` 心跳与其他消息的交错

缓解：使用事件驱动架构，每种 AS 消息类型注册独立处理器，避免阻塞式等待。

### 风险 4：DebugAdapterVersion 兼容性

AS 协议的部分消息格式依赖 `DebugAdapterVersion`（例如 `FAngelscriptVariable` 在 version >= 2 时多出 `ValueAddress` 和 `ValueSize` 字段）。适配器发送的 `DebugAdapterVersion` 需要与服务器侧当前支持的最高版本对齐（当前为 2），并处理未来版本扩展。

缓解：适配器硬编码发送 `DebugAdapterVersion = 2`，并在连接时验证服务器回复的 `DebugServerVersion`。

### 风险 5：FString 负长度编码

UE 的 `FArchive << FString` 在处理 UTF-8 内容时可能使用负长度值表示 UTF-8 编码（正值表示 UTF-16）。需要验证 AS 调试服务器实际发送的 FString 编码方式。

缓解：优先按 UE 标准 FString 序列化实现（正长度 = 字符数包含 null 终止符 + UTF-16LE），测试时用实际 UE 实例验证。
