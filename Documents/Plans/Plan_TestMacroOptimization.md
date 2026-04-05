# 测试宏优化计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `AngelscriptTest` 模块中的测试宏体系从“已有方案 + 局部落地 + 大量历史残留”推进到“宏接口稳定、可迁移文件清单明确、验证与文档同步”的可执行状态，后续执行者可以按目录批量迁移而不再重复摸底。

**Architecture:** 方案以 `Shared/AngelscriptTestMacros.h` 的 `ASTEST_*` 两层宏为唯一主线：先收口宏基础设施与验证入口，再按目录批量迁移仍直接依赖 `AcquireFreshSharedCloneEngine` / `AcquireCleanSharedCloneEngine` / `GetOrCreateSharedCloneEngine` 的测试，最后把 Native / 生产引擎 / 多引擎等不适合宏化的路径明确标记为保留项而不是继续混在 backlog 里。

**Tech Stack:** Unreal Engine Automation Tests、`Plugins/Angelscript/Source/AngelscriptTest/*`、`Tools/RunBuild.ps1`、`Tools/RunTests.ps1`、`Tools/RunTestSuite.ps1`、`Documents/Guides/Build.md`、`Documents/Guides/Test.md`

---

## 背景与目标

当前仓库关于测试宏优化已经不是“从零开始设计”的阶段，而是进入了**方案已存在、落地已开始、计划文本已落后于源码**的阶段。直接继续照旧计划推进，会带来三类问题：

1. **计划事实陈旧**
   - 当前 `Plan_TestMacroOptimization.md` 仍把“已完成”理解为只迁移了 `AngelscriptControlFlowTests.cpp` 与 `AngelscriptTypeTests.cpp`。
   - 实际源码中，`Angelscript/` 目录下已经有 **11 个文件**使用 `ASTEST_CREATE_ENGINE_*`，说明第一轮试点已经明显扩大，计划必须先更新到真实现状。
2. **候选改动面远大于原文估计**
   - 通过对 `AcquireFreshSharedCloneEngine` / `AcquireCleanSharedCloneEngine` / `GetOrCreateSharedCloneEngine` 的实际检索，当前 `AngelscriptTest` 模块里仍有 **70 个 `.cpp` 文件**直接依赖旧 helper 路径。
   - 这些文件并不只分布在 `Bindings/`、`Actor/`、`HotReload/`，还扩散到 `Compiler/`、`FileSystem/`、`Preprocessor/`、`ClassGenerator/`、`Internals/`、`Learning/Runtime/`、`Template/` 与 `Validation/`。
3. **宏文档与验证入口漂移**
   - `Shared/AngelscriptTestMacros.h` 当前已经只有 `ASTEST_*` 宏体系，但 `Validation/AngelscriptMacroValidationTests.cpp`、`Validation/AngelscriptCompilerMacroValidationTests.cpp` 以及多份宏文档仍在使用或描述旧的 `ANGELSCRIPT_TEST` / `ANGELSCRIPT_ISOLATED_TEST`。
   - 如果不先把“哪份文档是权威、哪些文件只是历史遗留”说清楚，后续迁移会持续出现“源码已切新宏，文档和验证仍在讲旧宏”的双轨状态。

本计划的目标不是在一轮里无脑替换所有测试，而是：

- 固定 **`ASTEST_*` 是当前唯一权威宏体系** 这一事实；
- 明确 **哪些文件已经迁移、哪些文件应继续迁移、哪些文件应保持不宏化**；
- 把后续执行拆成可批量推进的目录波次，并绑定标准 build/test 验证入口；
- 同步收口历史宏文档，避免后续执行者继续以旧宏方案为依据扩散新改动。

## 范围与边界

- **范围内**
  - `Plugins/Angelscript/Source/AngelscriptTest/Shared/`
  - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/`
  - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/`
  - `Plugins/Angelscript/Source/AngelscriptTest/{Actor,Blueprint,Component,Delegate,GC,HotReload,Interface,Inheritance,Subsystem}/`
  - `Plugins/Angelscript/Source/AngelscriptTest/{Compiler,FileSystem,Preprocessor,ClassGenerator,Internals,Learning/Runtime,Template,Validation}/`
  - `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
  - `Plugins/Angelscript/Source/AngelscriptTest/Shared/{README_MACROS.md,MACRO_ANALYSIS.txt,MACRO_MIGRATION_GUIDE.txt}`
  - 仓库根目录的 `MACRO_SYSTEM_README.md`、`MACRO_VALIDATION_REPORT.md`、`MACRO_IMPLEMENTATION_SUMMARY.md`
  - 本计划文档 `Documents/Plans/Plan_TestMacroOptimization.md`
- **范围外**
  - `Source/AngelscriptProject/` 宿主工程逻辑
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 里的 Runtime C++ 测试层
  - `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 里的 Editor 私有测试层
  - 与宏迁移无关的功能性修复；若迁移暴露真实 bug，应拆到单独 bugfix 任务
- **边界约束**
  - 只通过 `Tools/RunBuild.ps1` 与 `Tools/RunTests.ps1` / `Tools/RunTestSuite.ps1` 验证，不写手拼 `UnrealEditor-Cmd.exe` 命令。
  - `Native/`、`Learning/Native/`、生产引擎、多引擎、性能路径先做**判定**，不把它们强行塞进 `ASTEST_*`。
  - 中文计划优先；代码路径、Automation 前缀、类名保持英文。

## 当前事实状态

### 已确认的事实

1. `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` 已经定义当前有效的两层宏体系：
   - 引擎创建：`ASTEST_CREATE_ENGINE_FULL/SHARE/CLONE/NATIVE()`
   - 生命周期：`ASTEST_BEGIN_* / ASTEST_END_*`
   - Helper：`ASTEST_COMPILE_RUN_INT`、`ASTEST_COMPILE_RUN_INT64`、`ASTEST_BUILD_MODULE`
2. 以下 **11 个 `Angelscript/` 测试文件** 已使用 `ASTEST_*`，它们应被视为现有样板，而不是“仍待首批试点”：
   - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptHandleTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptInheritanceTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp`
3. 以下 **70 个 `.cpp` 文件** 仍直接依赖 `AcquireFreshSharedCloneEngine` / `AcquireCleanSharedCloneEngine` / `GetOrCreateSharedCloneEngine`，它们是宏迁移的主战场。
4. 以下文件仍直接描述或使用旧的 `ANGELSCRIPT_TEST` 体系，且在当前仓库里**没有检索到对应定义**：
   - `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptCompilerMacroValidationTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md`
   - `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_ANALYSIS.txt`
   - `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_MIGRATION_GUIDE.txt`
   - `MACRO_SYSTEM_README.md`
   - `MACRO_VALIDATION_REPORT.md`
5. `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` 当前依赖 `RequireRunningProductionEngine(...)`，属于**生产引擎路径**，不应被误归类为普通 shared/clone 宏迁移样本。
6. Native / ASSDK 相关测试仍大量使用 `CreateASSDKTestEngine`、`DestroyNativeEngine`、`asCreateScriptEngine` 等路径，按现状更适合作为“不宏化保留项”而不是 ASTEST 改造目标。

### 当前问题 → 对应解决方向

| 问题 | 当前症状 | 解决方向 |
| --- | --- | --- |
| 计划落后于源码 | 已迁移样板、真实待迁移目录和旧宏文档都没在 Plan 里体现 | 先把文件清单和现状补齐，再开始执行 |
| 宏权威来源不唯一 | `ASTEST_*` 已落地，但 Validation / README / 根目录报告仍在讲旧宏 | 固定 `TESTING_GUIDE.md + AngelscriptTestMacros.h` 为权威，并清理/降级旧文档 |
| 迁移面被低估 | 原计划只点名少量目录，实际 shared helper 用户已扩散到 70 个 `.cpp` | 以目录分组重写影响范围和分批策略 |
| 不同引擎路径被混在一起 | shared / fresh / production / native / custom create 路径交织 | 先分类为“可宏化 / 待评估 / 保持不宏化”再改 |

## 影响范围

本次测试宏优化涉及以下操作（按需组合）：

- **宏基础设施收口**：以 `ASTEST_*` 为唯一权威宏接口，必要时调整宏实现、helper API 或自测。
- **已迁移样板复核**：检查已使用 `ASTEST_*` 的样板文件是否仍残留旧 helper 语义或需要对齐最终宏风格。
- **shared helper 迁移**：把旧的 `AcquireFreshSharedCloneEngine` / `AcquireCleanSharedCloneEngine` / `GetOrCreateSharedCloneEngine` 调用改为合适的 `ASTEST_CREATE_ENGINE_* + ASTEST_BEGIN_* / ASTEST_END_*` 组合。
- **自定义引擎路径判定**：对 production / Native / ASSDK / 多引擎 / 性能路径明确标记“保持原样”或“单独处理”，避免误替换。
- **验证与文档同步**：把旧宏验证入口、迁移说明和总结文档统一到当前宏体系口径。

按目录分组的影响范围如下：

Shared/ 与宏文档（11 个）：
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` — 宏基础设施收口
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` — helper / 引擎获取兼容层收口
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h` — helper API 对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` — helper 实现配套调整
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` — helper 自测迁移与回归
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptLearningTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` — 作为权威宏文档继续维护
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md` — 旧宏 README，需更新或降级为跳转说明
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_ANALYSIS.txt` — 旧宏分析材料，需更新或归档
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_MIGRATION_GUIDE.txt` — 旧宏迁移说明，需重写到 ASTEST 口径
- `Documents/Plans/Plan_TestMacroOptimization.md` — 当前计划本身

Angelscript/ 已迁移样板与特殊路径（12 个）：
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` — 已迁移样板复核
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` — 已迁移样板复核
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` — 已迁移样板复核
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` — 已迁移样板复核
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptHandleTests.cpp` — 已迁移样板复核
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptInheritanceTests.cpp` — 已迁移样板复核
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` — 已迁移样板复核
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp` — 已迁移样板复核
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp` — 已迁移样板复核
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` — 已迁移样板复核
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` — 已迁移样板复核
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` — 生产引擎路径，判定为保持不宏化并记录原因

Bindings/（15 个）：
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerCompareBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGlobalBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptIteratorBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` — shared helper 迁移

Fast / 核心工具链候选（19 个）：
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` — shared helper 迁移，但需保留 create/destroy 语义边界
- `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBytecodeTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDataTypeTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptParserTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptScriptNodeTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptStructCppOpsTests.cpp` — shared helper 迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_Blueprint.cpp` — 夹具/模板同步更新
- `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_BlueprintWorldTick.cpp` — 夹具/模板同步更新
- `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_WorldTick.cpp` — 夹具/模板同步更新
- `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp` — 旧宏验证入口迁移
- `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptCompilerMacroValidationTests.cpp` — 旧宏验证入口迁移

UE 场景与 HotReload 候选（21 个）：
- `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorInteractionTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorLifecycleTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorPropertyTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptScriptSpawnedActorOverrideTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintSubclassActorTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintSubclassRuntimeTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateScenarioTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptGCScenarioTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` — shared/fresh helper 迁移，需保留 reload 语义
- `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` — shared/fresh helper 迁移，需保留 reload 语义
- `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` — shared/fresh helper 迁移，需谨慎处理性能语义
- `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` — shared/fresh helper 迁移，需保留 reload 语义
- `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` — shared/fresh helper 迁移，需保留 scenario + reload 语义
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceDeclareTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Inheritance/AngelscriptInheritanceScenarioTests.cpp` — `CLONE`/scenario 宏迁移候选
- `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` — `CLONE`/scenario 宏迁移候选

Learning/Runtime（13 个）：
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningBlueprintSubclassTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningClassGenerationTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningComponentHierarchyTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningDelegateBridgeTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningGCTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningInterfaceDispatchTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningReloadAndClassAnalysisTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningScriptClassToBlueprintTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp` — shared helper 迁移 / trace 样板对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningUEBridgeTraceTests.cpp` — shared helper 迁移 / trace 样板对齐

根目录宏总结材料（3 个）：
- `MACRO_SYSTEM_README.md` — 项目级宏说明，需改到 ASTEST 口径或显式标记历史状态
- `MACRO_VALIDATION_REPORT.md` — 宏验证报告，需同步实际验证入口与文件清单
- `MACRO_IMPLEMENTATION_SUMMARY.md` — 实现摘要，需更新“已迁移范围”与“剩余目录”

明确排除 / 单独评估（不纳入本轮直接宏迁移）：
- `Plugins/Angelscript/Source/AngelscriptTest/Native/*` — Native / ASSDK 测试，依赖 `CreateASSDKTestEngine`、`DestroyNativeEngine`、`asCreateScriptEngine`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Native/*` — Native learning trace，依赖原生引擎路径
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` — 自定义 create path，先判定是否保留原样
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp` — 性能/隔离语义优先，先判定是否保留原样
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp` — 自定义 create path，先判定是否保留原样

## 分阶段执行计划

### Phase 0：先收口事实与权威口径

- [ ] **P0.1** 固定 `ASTEST_*` 为唯一权威宏体系
  - 先以 `Shared/AngelscriptTestMacros.h` 和 `TESTING_GUIDE.md` 为准，逐条比对 Validation 与历史文档中仍在出现的 `ANGELSCRIPT_TEST` / `ANGELSCRIPT_ISOLATED_TEST` 叙述。
  - 若确实没有兼容定义，就把这些旧宏视为历史残留而不是“仍受支持接口”；计划、README、报告必须同步改口径。
  - 这一阶段的输出是“当前有效宏 API 清单 + 当前无效历史宏清单”，不直接开始批量替换测试文件。
- [ ] **P0.1** 📦 Git 提交：`[Test/Macro] Docs: align canonical macro API with current ASTEST system`

- [ ] **P0.2** 复核已迁移样板文件
  - 对 `Angelscript/` 下已使用 `ASTEST_*` 的 11 个文件做一次样板审查，确认是否仍残留旧 helper 依赖、是否存在 `FULL/SHARE/CLONE` 选择不一致、以及 `ASTEST_BEGIN_* / ASTEST_END_*` 是否已经覆盖清理语义。
  - 这一步的目的不是再扩大迁移范围，而是先让现有样板成为后续目录批迁的可靠模板。
  - 若发现宏 API 仍需微调，必须先改 `Shared/` 层，再继续 Phase 1 之后的目录批迁。
- [ ] **P0.2** 📦 Git 提交：`[Test/Macro] Refactor: stabilize existing ASTEST migration samples`

### Phase 1：收口宏基础设施与验证入口

- [ ] **P1.1** 对齐 `Shared/` helper 与宏实现边界
  - 只在 `AngelscriptTestMacros.h`、`AngelscriptTestUtilities.h`、`AngelscriptTestEngineHelper.*` 中处理共性问题：shared/fresh/clean 语义、生命周期清理、常用 compile/run helper、必要的 trace 或 class lookup 支撑。
  - 不要在每个测试文件里发明一套新的局部 helper；共性行为先回收到 `Shared/`，这样后续目录迁移才不会重复拷贝样板。
  - `Shared/AngelscriptTestEngineHelperTests.cpp` 与 `Shared/AngelscriptLearningTraceTests.cpp` 要同步覆盖这些共性语义，避免宏改完只有业务测试覆盖、没有 helper 自测。
- [ ] **P1.1** 📦 Git 提交：`[Test/Macro] Refactor: align shared helpers with ASTEST lifecycle model`

- [ ] **P1.2** 修正 Validation 与迁移说明入口
  - 把 `Validation/AngelscriptMacroValidationTests.cpp` 与 `Validation/AngelscriptCompilerMacroValidationTests.cpp` 从旧 `ANGELSCRIPT_TEST` 口径迁到当前 ASTEST 口径，确保验证入口本身不再依赖已淘汰的宏名称。
  - 同步更新 `README_MACROS.md`、`MACRO_ANALYSIS.txt`、`MACRO_MIGRATION_GUIDE.txt`，明确它们是继续维护还是降级为“历史方案说明 + 跳转到 `TESTING_GUIDE.md`”。
  - 如果决定保留这些旧文档，必须把示例代码和术语统一成 `ASTEST_*`；如果决定不再维护，至少要加明显的 deprecation 指引。
- [ ] **P1.2** 📦 Git 提交：`[Test/Macro] Docs: migrate validation and legacy macro docs to ASTEST`

### Phase 2：批量迁移 Fast / Bindings / 工具链测试

- [ ] **P2.1** 迁移 `Bindings/` 目录
  - `Bindings/` 这 15 个文件当前是最适合批量迁移的目录：大多属于无 World 的 shared-engine 快速回归，模式集中、收益高。
  - 每个文件先按现有 helper 路径判断是 `SHARE` 还是 `CLONE` 更合适，再统一引入 `ASTEST_CREATE_ENGINE_*` 与必要 helper 宏，避免只做机械文本替换。
  - 迁移后至少跑 `Angelscript.TestModule.Bindings.*` 前缀回归，确保绑定、容器、GameplayTag、控制台、Compat 等子主题没有因引擎清理语义变化引入回归。
- [ ] **P2.1** 📦 Git 提交：`[Test/Bindings] Refactor: migrate bindings tests to ASTEST macros`

- [ ] **P2.2** 迁移 Fast / 工具链目录
  - 处理 `Compiler/`、`FileSystem/`、`Preprocessor/`、`ClassGenerator/`、`Internals/`、`Template/` 以及 `Core/AngelscriptEngineCoreTests.cpp` 这批以 shared helper 为主的快速测试。
  - `Core/AngelscriptEngineCoreTests.cpp` 需要特别注意“引擎 create/destroy 样例”不能被宏化过度而失去原测试意图；必要时保留局部自定义路径，只迁移重复样板部分。
  - `Template/*.cpp` 是后续新增测试的脚手架样本，迁移它们的价值不只是消灭旧 helper，更是防止未来新增 case 继续复制旧写法。
- [ ] **P2.2** 📦 Git 提交：`[Test/Fast] Refactor: migrate fast-path helper consumers to ASTEST`

### Phase 3：批量迁移 UE 场景与 HotReload 测试

- [ ] **P3.1** 迁移 Actor / Blueprint / Component / Delegate / GC / Interface / Inheritance / Subsystem
  - 这些目录主要依赖 `AcquireFreshSharedCloneEngine` 或 `AcquireCleanSharedCloneEngine`，理论上最接近 `CLONE` 路径，但每个目录都要先确认是否需要在 `ASTEST_END_*` 之外补额外 reset 行为。
  - 迁移时优先保持现有 Automation 前缀和断言结构不变，只替换重复引擎获取/清理样板，避免把一次“宏收口”扩大成测试语义重写。
  - 这一批完成后，至少回归 `Angelscript.TestModule.Actor.*`、`Blueprint.*`、`Component.*`、`Delegate.*`、`GC.*`、`Interface.*`、`Subsystem.*`。
- [ ] **P3.1** 📦 Git 提交：`[Test/Scenario] Refactor: migrate scenario test helpers to ASTEST`

- [ ] **P3.2** 单独处理 HotReload 目录
  - `HotReload/` 目录虽然也使用 shared/fresh helper，但它们同时承担 soft reload / full reload / property preservation / performance 等判定语义，不能像 Bindings 一样机械替换。
  - 需要先按测试意图区分：哪些只是 shared clone 的获取样板，哪些实际上依赖特定 reset 顺序、reload 结果枚举或场景 spawner 初始化；必要时为 HotReload 保留更显式的局部清理代码。
  - 这一步的目标是“宏替换后不改变 reload 语义”，而不是追求 HotReload 文件长得和普通 shared test 一样短。
- [ ] **P3.2** 📦 Git 提交：`[Test/HotReload] Refactor: migrate hot reload tests without changing reload semantics`

### Phase 4：Learning/Runtime 与文档收口

- [ ] **P4.1** 迁移 `Learning/Runtime/` trace 样板
  - `Learning/Runtime/` 的价值是教学与可观测性，因此这批文件迁移时要特别注意保留 trace 文案、注释结构和教学节奏，不要为了抽象而把可读性牺牲掉。
  - 对于本质上只是 shared helper 样板的部分，可迁到 ASTEST；若某个 trace 依赖更细粒度的引擎控制，则记录为“保留局部 helper”并在文档中说明原因。
  - 最终目标是让 learning trace 也成为“当前宏体系下的标准教学样本”，而不是历史 helper 的最后避风港。
- [ ] **P4.1** 📦 Git 提交：`[Test/Learning] Refactor: align runtime learning traces with ASTEST patterns`

- [ ] **P4.2** 更新根目录宏总结材料
  - `MACRO_SYSTEM_README.md`、`MACRO_VALIDATION_REPORT.md`、`MACRO_IMPLEMENTATION_SUMMARY.md` 当前仍停留在第一轮宏方案叙事，需要改成“ASTEST 已经成为主线、哪些目录已迁、哪些目录仍保留原样”的现状口径。
  - 这些根目录文档不是执行入口，但它们会强烈影响后来者对项目状态的判断；如果不改，会继续误导后续任务把旧宏当成活跃接口。
  - closeout 时要确保根目录摘要与 `TESTING_GUIDE.md`、Validation 文件、计划文档表述一致。
- [ ] **P4.2** 📦 Git 提交：`[Docs/Test] Docs: refresh macro system summary and validation reports`

### Phase 5：明确不宏化保留项并完成验证

- [ ] **P5.1** 标记保持不宏化的路径
  - `AngelscriptNativeScriptHotReloadTests.cpp`、`Native/*`、`Learning/Native/*`、`Core/AngelscriptBindConfigTests.cpp`、`Core/AngelscriptEnginePerformanceTests.cpp`、`Internals/AngelscriptRestoreTests.cpp` 这批文件不能继续作为“以后再看看”的模糊 backlog。
  - 要么给出具体保留理由（production engine、ASSDK adapter、多引擎、性能语义、自定义 create path），要么在迁移过程中证明它们其实可以进入 ASTEST；不能既不迁也不解释。
  - 如果最终保留原样，不应阻塞本计划 closeout，但必须在文档里写成明确结论。
- [ ] **P5.1** 📦 Git 提交：`[Test/Macro] Docs: classify non-macroized engine paths and exclusions`

- [ ] **P5.2** 用标准入口完成 build/test 收口
  - 先读取根目录 `AgentConfig.ini`，然后只通过 `Tools/RunBuild.ps1` 和 `Tools/RunTests.ps1` / `Tools/RunTestSuite.ps1` 进行验证。
  - 优先做针对性回归：Validation、Shared.EngineHelper、Angelscript、Bindings、HotReload、Learning.Runtime；最后再按 `AngelscriptFast` 和 `AngelscriptScenario` group 做收口。
  - 如果仍有 Native / production / performance 路径未迁移，但它们已被明确标记为保留项，不应阻塞本计划归档。
- [ ] **P5.2** 📦 Git 提交：`[Test/Macro] Chore: close out macro migration plan with verified groups`

## 验收标准

1. `Shared/AngelscriptTestMacros.h` 与 `TESTING_GUIDE.md` 成为当前唯一权威宏口径，仓库内不再把 `ANGELSCRIPT_TEST` / `ANGELSCRIPT_ISOLATED_TEST` 当成活跃接口描述。
2. `Validation/AngelscriptMacroValidationTests.cpp` 与 `Validation/AngelscriptCompilerMacroValidationTests.cpp` 已迁到当前 ASTEST 口径，`grep` 旧宏名称只应命中明确标记为历史文档的文件。
3. `Bindings/`、Fast 工具链目录、UE 场景目录、`Learning/Runtime/` 的 shared helper 调用已按计划收敛，后续新增测试不再继续复制旧 helper 模板。
4. `AngelscriptNativeScriptHotReloadTests.cpp`、`Native/*`、`Learning/Native/*`、性能/自定义引擎路径等非宏化文件已经在计划或文档里有明确“保留原因”。
5. 至少完成以下标准验证并保留结果：
   - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -TimeoutMs 180000 -- -NoXGE`
   - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Validation." -TimeoutMs 600000`
   - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Shared.EngineHelper." -TimeoutMs 600000`
   - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Bindings." -TimeoutMs 600000`
   - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.HotReload." -TimeoutMs 600000`
   - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -Group AngelscriptFast -TimeoutMs 600000`
   - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -Group AngelscriptScenario -TimeoutMs 600000`

## 风险与注意事项

### 风险

1. **`AcquireFreshSharedCloneEngine` 与 `AcquireCleanSharedCloneEngine` 语义并不完全等价**
   - 一旦简单替换成统一的 `ASTEST_CREATE_ENGINE_CLONE()`，可能把“每次 fresh reset”与“显式 clean reset”之间的差异抹掉，导致跨 case 状态污染。
   - **缓解**：先在 `Shared/` 层澄清 `SHARE / CLONE / FULL` 的清理模型，再分目录迁移。

2. **HotReload 与生产引擎路径容易被过度抽象**
   - HotReload、NativeScriptHotReload、性能/restore 路径本来就在测试边界行为；如果为了套宏而隐藏关键 reload / create / teardown 细节，测试会变短但语义会变弱。
   - **缓解**：把“保留局部显式逻辑”作为允许选项，而不是要求所有文件都长成同一种宏样板。

3. **旧宏文档可能继续误导后续执行者**
   - 即使代码迁完，只要根目录和 `Shared/` 下的第一轮宏文档不更新，后续新任务仍可能根据旧宏名称继续提交新改动。
   - **缓解**：把文档清理列为正式 Phase，而不是 closeout 前顺手补一笔。

### 已知行为变化

1. **Validation 样板会改名义上的宏族**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptCompilerMacroValidationTests.cpp`
   - 说明：这些文件后续会从旧 `ANGELSCRIPT_TEST` 描述迁到 `ASTEST_*` 口径；验证逻辑应保留，但样板代码形态会变化。

2. **`README_MACROS.md` / `MACRO_ANALYSIS.txt` / `MACRO_MIGRATION_GUIDE.txt` 很可能降级为历史说明**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_ANALYSIS.txt`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_MIGRATION_GUIDE.txt`
   - 说明：如果决定由 `TESTING_GUIDE.md` 独占权威说明，这些文件要么改成 ASTEST 口径，要么改成明确的 deprecation/redirect 文档。

3. **部分测试会从显式 helper 获取切到标准 ASTEST 宏入口**
   - 影响目录：`Bindings/`、`Actor/`、`Blueprint/`、`Component/`、`Delegate/`、`GC/`、`HotReload/`、`Interface/`、`Inheritance/`、`Subsystem/`、`Learning/Runtime/`
   - 说明：文件内的引擎获取、scope、清理样板会收敛，但 Automation 前缀、断言文案和测试语义不应因此发生无关变化。

## 依赖关系

```text
Phase 0 统一事实与权威口径
  ↓
Phase 1 收口 Shared/ 与 Validation 入口
  ↓
Phase 2 迁移 Bindings / Fast 工具链目录
  ↓
Phase 3 迁移 Scenario / HotReload 目录
  ↓
Phase 4 收口 Learning/Runtime 与根目录文档
  ↓
Phase 5 标记不宏化保留项并完成验证
```

## 参考文档

| 文档 | 用途 |
| --- | --- |
| `Documents/Plans/Plan.md` | Plan 结构、影响范围章节、checkbox 规则 |
| `Documents/Guides/Build.md` | 标准构建入口与超时规则 |
| `Documents/Guides/Test.md` | 标准测试入口与回归命令 |
| `Documents/Guides/TestConventions.md` | 测试层级、目录、Automation 前缀规则 |
| `Plugins/Angelscript/AGENTS.md` | `AngelscriptTest` 模块的放置与命名边界 |
| `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` | 当前宏体系说明与示例 |
