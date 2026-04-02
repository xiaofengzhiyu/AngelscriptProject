# Angelscript 插件 Mod 支持方案探索计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 按任务执行本计划。步骤使用 checkbox（`- [ ]`）语法跟踪。

**Goal:** 基于当前 `Plugins/Angelscript` 的真实能力边界，产出一份可落地的 Mod 支持方案判断结果：明确现有基础设施、外部可借鉴模式、推荐 MVP 路线，以及刻意暂缓的高风险方向，而不是直接跳到实现。

**Architecture:** 本计划按“当前事实盘点 → 外部模式对照 → 候选方案收敛 → 最小原型验证 → 验证与文档收口”推进。探索重心放在 `AngelscriptRuntime` 已存在的脚本根发现、模块编译、热重载、依赖注入、预处理与访问限制能力上，并把 Unreal 侧的插件/Game Feature/Pak 挂载路线与 AngelScript 原生模块/access mask/bytecode 模式分开对照，避免把不同层级能力混写成一个笼统的“mod 系统”。

**Tech Stack:** Unreal Engine 插件模块（`AngelscriptRuntime` / `AngelscriptEditor` / `AngelscriptTest`）、`FAngelscriptEngine`、`FAngelscriptEngineDependencies`、`FAngelscriptPreprocessor`、`FAngelscriptRuntimeModule`、AngelScript `asIScriptModule` / imports / shared / access mask / bytecode、UE PluginManager / AssetManager / HotReload 相关机制。

---

## 背景与目标

当前仓库的主目标不是把宿主工程继续做大，而是把 `Plugins/Angelscript` 整理成可复用、可验证、可维护的 Unreal Angelscript 插件。用户这次要解决的问题不是“立刻实现 mod”，而是先回答下面几件事：

- 当前插件是否已经具备支撑 Mod 的底座，例如多脚本根发现、模块拆分、动态编译、热重载、按来源隔离脚本目录；
- 哪些能力只是“看起来像 mod 能力”，实际上仍停留在测试 seam、Editor-only 约束或单机开发便利层；
- 如果要给插件增加 Mod 支持，第一版最值得探索的是哪条路线：脚本根注入、插件化脚本分发、Pak/DLC 挂载、还是更强隔离的多引擎/权限模型；
- 哪些方向虽然理论上可行，但在当前插件化目标下风险过高，应该明确延后，而不是混进 MVP。

本计划的目标是把这些问题拆成可执行的探索阶段，让后续执行者能够围绕明确证据推进，不需要再次从零梳理整个仓库与外部参考。

## 范围与边界

- **纳入范围**
  - `Plugins/Angelscript/Source/AngelscriptRuntime/` 中与脚本根发现、模块管理、编译、热重载、依赖注入、访问限制相关的核心能力。
  - `Plugins/Angelscript/Source/AngelscriptEditor/` 中与目录监听、内容浏览器、编辑器装载和类重载有关的支持能力。
  - `Plugins/Angelscript/Source/AngelscriptTest/` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 中能验证脚本来源发现、文件系统、热重载、多引擎与依赖注入的现有测试。
  - AngelScript 官方模块、imports/shared、access mask、bytecode、serializer 等公开能力；以及 Unreal 侧与脚本插件最相关的 mod 分发/加载模式。
- **不纳入范围**
  - 直接实现完整 Mod 管理器、运行时 UI、网络分发、创意工坊接入或跨版本兼容层。
  - 将 Hazelight 的引擎分支能力视为当前插件内可直接复制的现成功能。
  - 在本计划第一轮把 packaged/cooked、多人同步、安全沙箱、IDE 工作流、Pak 加密等所有问题一次性拉平。
- **关键约束**
  - 所有结论必须优先服务于“插件化、可维护”的目标，不能为了追求理论最强隔离而把方案推回宿主工程或强依赖引擎分支。
  - 若某个候选路线需要引擎补丁、闭源 Hazelight 细节或明显超出当前插件边界的系统重构，必须显式标记为“后续研究项”，不能伪装成第一版 MVP。

## 当前事实状态快照

- 当前插件结构见 `Plugins/Angelscript/Angelscript.uplugin`：仅有 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块，尚无独立的 `Loader` / `Mod` / `Package` 模块。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 已具备以下关键底座：
  - `FAngelscriptEngineDependencies::GetEnabledPluginScriptRoots`：可注入插件脚本根发现逻辑；
  - `FAngelscriptEngine::DiscoverScriptRoots()`：可收集 project root + plugin roots；
  - `FAngelscriptEngine::CompileModules()` / `DiscardModule()` / `SwapInModules()`：可管理脚本模块的编译、丢弃与替换；
  - `FAngelscriptEngine::CheckForHotReload()` / `PerformHotReload()` / `CheckForFileChanges()`：可处理文件变化后的 soft/full reload；
  - `CheckUsageRestrictions()`：当前存在基于 wildcard 的模块依赖限制，但它是编译期/Editor 语义，不等于运行时 mod 沙箱。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` 暴露了 `OnProcessChunks` / `OnPostProcessCode` 两个预处理 hook，可作为注入 mod metadata、统一 include/import 规则或额外约束的候选挂点。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h` 暴露了 pre/post compile、pre-generate classes、literal asset created 等运行时委托，说明当前插件已经有对外事件面，但没有面向 mod 生命周期的专门事件。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp` 已验证 `DiscoverScriptRoots()` 能基于注入依赖收集 project/plugin script roots，并处理 project-only、missing root skip、Editor 自动创建脚本目录等情况。
- `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` 已验证磁盘 `.as` 文件发现、按文件名查找模块、从磁盘编译、局部失败保留其他模块等行为。
- 仓内不存在显式 `mod`、`ModLoader`、Pak 挂载、manifest/version/dependency/lifecycle API；当前能力更接近“多脚本根 + 动态编译/热重载基础设施”，而不是已完成的 Mod 框架。

## 候选路线前置假设

在正式收敛方案之前，先固定三条需要对照的候选路线，避免执行中不断漂移：

1. **脚本根注入路线**：把 Mod 看成额外的脚本根或额外启用的脚本插件目录，优先复用 `DiscoverScriptRoots()`、`GetEnabledPluginScriptRoots`、`CompileModules()`、`SwapInModules()`。
2. **插件/包分发路线**：把 Mod 看成带 `.uplugin` / manifest 的插件化分发单元，必要时结合 Game Feature、Data Registry、AssetManager 与 precompiled cache，再决定是 source scripts 还是 bytecode 进入现有引擎。
3. **强隔离路线**：把 Mod 看成独立执行上下文，探索 `CreateCloneFrom()`、AngelScript `accessMask`、imports/shared 以及更细权限模型是否足以支撑更强边界。

本计划不预设三条路线谁一定胜出，但要求后续 Phase 明确给出：哪条适合作为 MVP，哪条只适合作为后续增强，哪条目前不值得做。

## 分阶段执行计划

### Phase 0：冻结问题定义、术语与非目标

> 目标：先把“这里说的 mod 到底是什么、先不解决什么”写死，避免后面把脚本插件、Game Feature、Pak、DLC、运行时热修复等概念混成一个大词。

- [ ] **P0.1** 固定当前讨论中的 `mod` 术语与成功标准
  - 结合本计划、`Plugins/Angelscript/Angelscript.uplugin`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 里的现有能力，明确本轮探索至少要区分三类对象：`script-only mod`、`plugin-contained mod`、`content + script mod`。
  - 成功标准至少要回答四个问题：Mod 如何被发现、如何进入脚本编译链、如何与已有模块通信、如何被更新/停用；不要用“支持 mod”这种不可验收的大句子收尾。
  - 这一项必须顺带固定术语：哪些地方统一写“脚本根”“模块”“mod manifest”“distribution unit”“isolation”，避免后续把 AngelScript module、UE plugin module、玩法 mod 三层概念混写。
- [ ] **P0.1** 📦 Git 提交：`[Docs/Mod] Plan: freeze mod support exploration terminology and success criteria`

- [ ] **P0.2** 固定本轮明确不做的高风险项
  - 结合 `Documents/Plans/Plan_HazelightCapabilityGap.md`、`Reference/README.md` 与当前插件目标，显式排除需要引擎补丁、闭源 Hazelight 细节、完整运行时 UI、在线下载/签名验证、跨二进制 bytecode 兼容与多人同步安全模型等内容。
  - 这一项的价值不是“保守”，而是防止探索计划被升级成无限 backlog；尤其要避免把 Game Feature、Pak、资产挂载、脚本 VM 隔离和安全沙箱在第一轮全部做成必选项。
  - 若某个方向被暂缓，也要写清楚是因为“超出插件边界”“证据不足”还是“适合第二阶段增强”，而不是简单删掉。
- [ ] **P0.2** 📦 Git 提交：`[Docs/Mod] Plan: freeze non-goals for first-pass mod support study`

### Phase 1：盘清当前插件里已经存在的 Mod 基础设施

> 目标：把“当前已经能复用什么”做成事实清单，而不是每次讨论都重新猜 `FAngelscriptEngine` 能不能做这件事。

- [ ] **P1.1** 盘清脚本来源发现与装载入口
  - 以 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 中 `FAngelscriptEngineDependencies::CreateDefault()`、`GetEnabledPluginScriptRoots` 和 `DiscoverScriptRoots()` 为主证据，梳理 project root、enabled plugin roots、project-only discovery 的收集顺序、去重规则、missing root 处理与 Editor 自动建目录行为。
  - 结合 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp`，明确哪些行为已经有自动化测试守护，哪些仍停留在默认实现而没有独立测试，例如运行期重新发现新目录、禁用/卸载后刷新 roots 等。
  - 输出必须明确写出：当前插件已经具备“多脚本根发现”能力，但这并不等于“运行时动态安装 mod”。
- [ ] **P1.1** 📦 Git 提交：`[Runtime/Mod] Docs: capture existing script-root discovery capabilities`

- [ ] **P1.2** 盘清模块编译、丢弃、替换与热重载边界
  - 以 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` / `.cpp` 中的 `CompileModules()`、`DiscardModule()`、`CheckForHotReload()`、`PerformHotReload()`、`SwapInModules()` 为证据，明确模块级别的替换/丢弃/失效标记与 reload 触发条件。
  - 结合 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` 与 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/` 主题测试，回答“磁盘上的新/改/删脚本如何映射到模块级动作”，以及“失败模块是否会拖垮其他模块”。
  - 这一项要特别标出：当前引擎的热重载是围绕文件变化和模块替换设计的，不等于已经拥有 mod enable/disable/update 生命周期 API。
- [ ] **P1.2** 📦 Git 提交：`[Runtime/Mod] Docs: map compile and hot-reload boundaries for mod study`

- [ ] **P1.3** 盘清现有隔离与权限原语
  - 以 `CheckUsageRestrictions()`、`FAngelscriptEngine::CreateCloneFrom()`、AngelScript `accessMask` 相关上游能力为线索，明确当前仓内已有的“隔离”到底属于：编译期模块依赖限制、测试/多引擎共享状态、还是可扩展成运行时权限模型的底座。
  - 这一项必须明确区分：`UsageRestrictions` 是基于 module name wildcard 的使用限制；`Clone` 引擎当前主要服务测试/共享状态场景；二者都不能在未验证前直接宣称为“mod sandbox”。
  - 若盘点后发现某个原语仅在理论上可扩展而工程代价过高，也要直接记为“低优先/后续研究”，不要继续把它留在 MVP 候选集中。
- [ ] **P1.3** 📦 Git 提交：`[Runtime/Mod] Docs: classify existing isolation primitives and limits`

### Phase 2：对照外部模式，避免自创一个不必要的大系统

> 目标：把可直接借鉴的 AngelScript/UE 模式和必须自研的部分拆开，避免后面因为“没公开样例”就要么过度乐观、要么过度悲观。

- [ ] **P2.1** 固定 AngelScript 原生模块化模式的可迁移边界
  - 以 AngelScript 官方 `module`、`imports`、`shared`、`access mask`、`SaveByteCode/LoadByteCode`、`CSerializer` 等公开能力为主证据，回答哪些机制可以直接映射为“每个 mod 一个脚本模块”“mod 间 imports/shared 通信”“bytecode 分发”“热重载状态保留”。
  - 这一项要特别标出两个边界：一是官方能力能解决脚本模块问题，但不直接提供 UE 侧的目录发现、资源挂载、插件生命周期；二是 bytecode 可分发不代表跨二进制/跨版本稳定可用。
  - 若要采用 `accessMask`，必须同步列出当前仓内哪些 C++ 绑定/注册路径需要补 access level，而不是把它当成零成本开关。
- [ ] **P2.1** 📦 Git 提交：`[Docs/Mod] Docs: record transferable AngelScript modularity patterns`

- [ ] **P2.2** 固定 Unreal 侧 Mod 分发/挂载候选路线的证据矩阵
  - 这一项要把 Unreal 侧最相关的几条路线拆开比较，并为每条路线补齐官方/社区证据：`Content-Only Plugin` 作为 mod 载体、`UGameFeaturesSubsystem` / `UGameFeatureAction` 承载运行期启停、Pak/DLC 挂载后进入脚本/资源发现链、`Data Registry` 承载 mod 配置或静态数据覆盖、`AssetManager` / `AssetRegistry` 负责 Primary Asset 与可见性管理。
  - 比较时必须显式记录每条路线与当前插件边界的关系：哪些天然适合当前已有的 `GetEnabledPluginScriptRoots` / `DiscoverScriptRoots()`（例如插件化脚本根发现），哪些更像运行时生命周期壳层（例如 Game Features），哪些主要解决 cooked 分发而不是开发期热重载（例如 Pak、precompiled cache）。
  - 这一项还要明确记录 AngelScript 特有分发点：Hazelight 的 `PrecompiledScript.Cache` / C++ transpilation 更适合 shipping 分发和启动优化，但不应盖过 development 模式下 loose `.as` 文件的迭代价值；如果某条 UE 路线只适合内容资源、不适合脚本源码工作流，也要明确写出来，而不是保留成“以后可能支持”的模糊项。
- [ ] **P2.2** 📦 Git 提交：`[Docs/Mod] Docs: compare Unreal-side mod distribution routes for AS plugin`

- [ ] **P2.3** 固定本地参考仓库中的工程边界启发
  - 结合 `Documents/Plans/Plan_HazelightCapabilityGap.md` 与 `Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md`，重点吸收两类信息：Hazelight 的 `Loader` / 启动收口思路，以及 UnrealCSharp 把生成/加载/增量更新拆成显式阶段的方式。
  - 这一项不是为了照搬 Hazelight 或 UnrealCSharp 的语言/引擎前提，而是为了判断：当前插件是否需要一个更显式的 loader/manifest/index 层，来承接未来 mod 发现、依赖和生命周期。
  - 若结论是“短期先不拆新公共模块，只在现有三模块内提炼内部支撑层”，也要作为正式结论记录下来。
- [ ] **P2.3** 📦 Git 提交：`[Docs/Mod] Docs: absorb Hazelight and UnrealCSharp boundary lessons`

### Phase 3：收敛候选方案，选出一个推荐 MVP

> 目标：不再停留在“理论上都能做”，而是强制输出推荐路线、放弃路线和判断依据。

- [ ] **P3.1** 形成三条候选方案的对比矩阵
  - 至少比较三条路线：A）基于 `DiscoverScriptRoots()` / plugin roots 的 script-root 注入方案；B）带 manifest 的插件/包分发方案；C）基于 clone engine / access mask 的强隔离方案。
  - 对比维度至少包括：对当前插件改动范围、是否依赖引擎补丁、脚本/资源发现复杂度、热重载体验、运行时 enable/disable 难度、调试/诊断成本、未来安全扩展空间。
  - 输出不能只写主观判断，必须把每一列都回链到前面 Phase 的证据，例如 `GetEnabledPluginScriptRoots` 已存在、`UsageRestrictions` 仅编译期可见、bytecode 跨二进制风险等。
- [ ] **P3.1** 📦 Git 提交：`[Docs/Mod] Plan: compare mod-support architecture candidates`

- [ ] **P3.2** 固定推荐 MVP 及其刻意延后的增强项
  - 在矩阵基础上选出一个推荐 MVP，并把“第二阶段增强”和“当前不建议推进”的内容同时写清楚。例如：第一版优先探索 script-root/plugin-based mod，延后真正的 Pak 分发与强沙箱，不把三件事一起绑死。
  - 推荐理由必须回答：为什么这条路线最符合当前插件化目标、最容易验证、最少误伤现有编译/热重载链；同时也要写出它暂时无法覆盖的能力，例如运行时安装 UI、强安全边界、跨包资源挂载等。
  - 如果推荐路线仍需要一个轻量 manifest/index 层，也要把它归类为 MVP 的组成部分还是 Phase 2 增强，而不是悬空。
- [ ] **P3.2** 📦 Git 提交：`[Docs/Mod] Plan: freeze recommended MVP route and deferred work`

### Phase 4：做最小验证原型，确认推荐路线不是纸上谈兵

> 目标：用最小 Spike 验证关键假设，不把整条路线建立在“看代码似乎可以”之上。

- [ ] **P4.1** 验证基于额外脚本根/插件脚本目录的最小装载闭环
  - 围绕 `FAngelscriptEngineDependencies::GetEnabledPluginScriptRoots`、`DiscoverScriptRoots()`、`CompileModules()`、`GetModuleByFilenameOrModuleName()` 做一个最小 Spike，确认额外脚本来源是否能以清晰的 module naming、diagnostics 和 reload 语义进入现有引擎。
  - 这一项不要求一开始就实现完整 manifest 或 UI，重点是验证“额外来源进入脚本发现与编译链”是否顺畅，以及失败/重载时会不会污染现有 project root 模块。
  - 测试优先落在现有 `AngelscriptDependencyInjectionTests.cpp`、`AngelscriptFileSystemTests.cpp`、`HotReload/` 主题附近，而不是新造一套脱离现有基础设施的实验工程。
- [ ] **P4.1** 📦 Git 提交：`[Runtime/Mod] Spike: validate injected script roots as mod entrypoint`

- [ ] **P4.2** 验证 manifest、依赖与模块命名的最小设计
  - 基于前面推荐路线，做一个最小 manifest/index 设计 Spike，至少回答：mod 名称/版本/依赖声明放在哪里，如何映射到 AngelScript module name，如何避免不同 mod 与 project root 下的模块名冲突。
  - 若推荐路线暂不引入 manifest，也要用 Spike 证明“仅靠目录结构 + module naming convention”能否满足第一版可维护性；否则就应尽早承认 manifest/index 是刚需，而不是实现后期再补。
  - 这一项要明确 imports/shared/依赖顺序与 manifest 之间的关系，防止后续同时维护两套彼此冲突的依赖模型。
- [ ] **P4.2** 📦 Git 提交：`[Runtime/Mod] Spike: validate manifest and module naming model`

- [ ] **P4.3** 验证隔离策略的现实成本
  - 选择一个最关键的隔离点做最小 Spike，例如：`UsageRestrictions` 能否扩展成 mod 间访问分层、`accessMask` 需要补多少绑定注册工作、`CreateCloneFrom()` 是否真的能承载生产级 mod 隔离，还是更像测试工具。
  - 这一项的目标不是把强隔离做完，而是尽早识别它是否值得列入第二阶段增强。如果代价明显过高，应在本轮探索结束时正式降级，而不是继续挂在 roadmap 上制造错觉。
  - 必须保留证据：是哪些绑定路径、哪些 shared state、哪些 reload/对象生命周期问题导致成本高，而不是只写“复杂”。
- [ ] **P4.3** 📦 Git 提交：`[Runtime/Mod] Spike: measure isolation strategy feasibility`

### Phase 5：收口验证矩阵、结论文档与后续 backlog

> 目标：把探索结果变成能交接、能继续执行的结论，而不是散落在一次性会话里。

- [ ] **P5.1** 固定探索期验证矩阵与最小自动化覆盖
  - 把本轮探索涉及的关键验证点写成矩阵：脚本来源发现、模块命名/查找、compile failure 隔离、reload 行为、manifest 解析、依赖顺序、隔离策略验证。明确哪些已有现成测试可复用，哪些需要新增最小测试 seam。
  - 这一步要同步更新 `Documents/Guides/Test.md` 或新增与 mod 探索相关的执行说明，至少让下一位执行者知道该跑哪些 test prefix、看哪些日志/报告，而不是重新从源码猜。
  - 如果某个 Spike 只能手工验证，也要把手工步骤写清楚，避免后续再次“口头通过”。
- [ ] **P5.1** 📦 Git 提交：`[Docs/Test] Docs: define validation matrix for mod support exploration`

- [ ] **P5.2** 输出最终结论与实施入口
  - 最终产出至少包含：当前插件已具备的基础设施清单、推荐 MVP 方案、明确延后项、关键风险、建议首先落地的实现任务列表。
  - 这份结论应回写到本计划或紧邻的设计/决策文档中，并给出下一阶段应该先开的 implementation plan 范围，而不是让后续执行者再重新从 Phase 0 做一次大盘点。
  - 若最终建议是“当前只值得支持 script-root/plugin-based mod，不建议承诺 Pak/强沙箱”，也要作为正式结论显式记录，不允许只留下模糊措辞。
- [ ] **P5.2** 📦 Git 提交：`[Docs/Mod] Docs: publish mod support exploration conclusion and backlog seed`

## 验收标准

1. 已明确写出当前插件对 Mod 支持最相关的真实基础设施，并能用具体路径回链到 `AngelscriptEngine`、`AngelscriptPreprocessor`、`AngelscriptRuntimeModule`、现有测试与参考计划文档。
2. 已形成至少三条候选路线的对比矩阵，并且每条路线都有“为何可行 / 为何不可行 / 是否适合 MVP”的证据说明。
3. 已明确选出一个推荐 MVP 路线，并把至少两类高风险方向列为延后项或非目标，而不是继续并列挂着。
4. 已给出最小 Spike 和验证矩阵，确保推荐路线不是纯概念判断。
5. 后续执行者可以直接依据本计划继续拆实现计划，而不需要重新全仓搜索当前能力与外部模式。

## 风险与注意事项

- **风险 1：把“多脚本根发现”误判成“运行时 mod 生命周期已具备”**
  - 当前 `DiscoverScriptRoots()` 与依赖注入测试证明了多来源脚本发现，但并没有天然回答运行时安装、停用、卸载、升级与状态恢复问题。

- **风险 2：把编译期限制原语误判成完整安全沙箱**
  - `CheckUsageRestrictions()` 和官方 `accessMask` 都是有价值的线索，但在未补绑定分层、运行时权限模型和对象生命周期验证前，不能对外承诺“安全 mod 沙箱”。

- **风险 3：把 UE 分发/挂载路线与脚本编译路线混成一件事**
  - Plugin/Game Feature/Pak 可以帮助分发或发现内容，但不等于自动解决 AngelScript 模块命名、编译顺序、热重载与 imports/shared 通信问题；同理，precompiled cache 能提升 shipping 启动速度，也不等于 development/mod authoring 体验已经被覆盖。

- **风险 4：高估 Hazelight 或闭源实现的可迁移性**
  - Hazelight 的价值更多在于边界启发，而不是可直接复制的源码方案；凡是依赖其引擎分支或未公开实现的部分，都必须单独降级处理。

- **风险 5：过早进入完整实现，导致探索任务失焦**
  - 本计划的首要目标是收敛方案与验证关键假设，不是把 manifest、Pak、运行时 UI、权限系统、多人同步一次做完。任何超出推荐 MVP 的扩展项都应另开 sibling plan。
