# 测试宏优化计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `AngelscriptTest` 的测试宏接入从“`ASTEST_CREATE_ENGINE_*` 已大面积铺开，但生命周期接入和旧 helper 收尾未闭环”推进到“`BEGIN/END` 口径统一、旧 helper 清零或明确保留、验证与文档可归档”的状态。

**Architecture:** 当前仓库已经以 `Shared/AngelscriptTestMacros.h` 和 `TESTING_GUIDE.md` 中的 `ASTEST_*` 体系为主线，Validation 与宏说明也已切到现行口径。剩余工作不再是重新设计宏方案，而是完成两条 closeout 主线：一是把批量迁移后的测试统一收口到 `ASTEST_BEGIN_* / ASTEST_END_*` 或明确记录的等价生命周期模式，二是消除仍直接调用、局部包装或隐藏依赖 `AcquireFreshSharedCloneEngine` / `AcquireCleanSharedCloneEngine` / `GetOrCreateSharedCloneEngine` 的尾项文件。

**Tech Stack:** Unreal Engine Automation Tests、`Plugins/Angelscript/Source/AngelscriptTest/*`、`Tools/RunBuild.ps1`、`Tools/RunTests.ps1`、`Tools/RunTestSuite.ps1`、`Documents/Guides/Build.md`、`Documents/Guides/Test.md`

---

## 背景与目标

这份计划已经不再处于“从零设计测试宏”的阶段。仓库在 `2026-04-05` 的提交 `1cd84bc`（`[Test/Automation] Refactor: sync ASTEST migration with validation workflows`）之前后，已经完成了大量 `ASTEST_CREATE_ENGINE_*` 铺开、Validation 切换以及宏文档同步。

当前真正没有收口的点，是用户指出的这件事：**批量改造测试接入 `BEGIN/END` 还没有完成**。也就是说，很多文件已经不用旧 helper 了，但仍没有统一到明确的生命周期入口；与此同时，仍有少数文件继续直接调用或通过局部 wrapper 保留历史 helper 语义。旧版本计划把主战场描述成“Validation 仍用旧宏、约 70 个文件还在 direct helper”，这已经不符合仓库现状，继续照旧执行会误导后续迁移顺序。

本次更新后的目标是：

- 固定“`ASTEST_*` 已经是当前唯一权威口径”这一事实。
- 把“批量补齐 `BEGIN/END` 生命周期接入”明确提升为当前首要剩余工作。
- 把“仍直接调用或局部包装旧 helper 语义的 10 个文件”从大而散的 backlog 收缩成清晰尾项清单。
- 把不适合宏化的 production / Native / custom create 路径明确标记为保留项，而不是继续与批量迁移任务混在一起。

## 范围与边界

- **范围内**
  - `Plugins/Angelscript/Source/AngelscriptTest/Shared/`
  - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/`
  - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/`
  - `Plugins/Angelscript/Source/AngelscriptTest/Core/`
  - `Plugins/Angelscript/Source/AngelscriptTest/{Actor,Blueprint,Component,Delegate,GC,HotReload,Interface,Inheritance,Subsystem}/`
  - `Plugins/Angelscript/Source/AngelscriptTest/{Compiler,FileSystem,Preprocessor,ClassGenerator,Internals,Learning/Runtime,Template,Validation,Examples}/`
  - `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
  - `Plugins/Angelscript/Source/AngelscriptTest/Shared/{README_MACROS.md,MACRO_ANALYSIS.txt,MACRO_MIGRATION_GUIDE.txt}`
  - 仓库根目录的 `MACRO_SYSTEM_README.md`、`MACRO_VALIDATION_REPORT.md`、`MACRO_IMPLEMENTATION_SUMMARY.md`
  - 本计划文件 `Documents/Plans/Plan_TestMacroOptimization.md`
- **范围外**
  - `Source/AngelscriptProject/` 宿主工程逻辑
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 中的 Runtime C++ 测试层
  - `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 中的 Editor 私有测试层
  - 与测试宏迁移无关的功能性修复；若在迁移时暴露真实 bug，应拆成独立 bugfix 任务
- **边界约束**
  - 只通过 `Tools/RunBuild.ps1` 与 `Tools/RunTests.ps1` / `Tools/RunTestSuite.ps1` 验证，不手写 `UnrealEditor-Cmd.exe` 命令。
  - 中文计划优先；代码路径、Automation 前缀、类型名保持英文。
  - `Native/`、`Learning/Native/`、production engine、多引擎、自定义 create path、性能路径先做归类，不强行纳入统一宏化。

## 当前事实状态

### 已确认的事实

1. `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` 与 `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` 已经共同定义了当前有效的 `ASTEST_*` 宏体系。
2. `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp` 与 `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptCompilerMacroValidationTests.cpp` 已经迁移到 `ASTEST_*` 口径，不再以 `ANGELSCRIPT_TEST` / `ANGELSCRIPT_ISOLATED_TEST` 作为当前接口。
3. 仓库内仍出现旧宏名称，但当前只出现在“历史说明 / 弃用说明”语境中，主要位于：
   - `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md`
   - `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_ANALYSIS.txt`
   - `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_MIGRATION_GUIDE.txt`
   - `MACRO_SYSTEM_README.md`
   - `MACRO_VALIDATION_REPORT.md`
   - `MACRO_IMPLEMENTATION_SUMMARY.md`
4. 当前已有 **13 个 `.cpp` 文件** 完成 `ASTEST_CREATE_ENGINE_* + ASTEST_BEGIN_* / ASTEST_END_*` 的全链路接入，它们应作为后续目录批量改造的正式样板，而不是继续混在待迁移范围里：
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
   - `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptCompilerMacroValidationTests.cpp`
5. 当前 `AngelscriptTest` 下已有 **72 个 `.cpp` 文件** 使用 `ASTEST_CREATE_ENGINE_*`。
6. 在这 72 个文件里，仍有 **59 个 `.cpp` 文件** 没有统一接入 `ASTEST_BEGIN_* / ASTEST_END_*`；这不是零散问题，而是当前最大的未完成批量改造面。按目录分布如下：
   - `Bindings/`：15
   - `Learning/Runtime/`：13
   - `Internals/`：8
   - `Interface/`：5
   - `HotReload/`：5
   - `Actor/`：3
   - `Blueprint/`：2
   - `Compiler/`、`FileSystem/`、`Core/`、`ClassGenerator/`、`GC/`、`Component/`、`Inheritance/`、`Subsystem/`：各 1
7. 当前仍有 **10 个 `.cpp` 文件** 直接调用、局部包装或隐藏依赖 `AcquireFreshSharedCloneEngine` / `AcquireCleanSharedCloneEngine` / `GetOrCreateSharedCloneEngine` 的旧 helper 语义，它们是旧 helper 收尾的明确尾项：
   - `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptScriptSpawnedActorOverrideTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateScenarioTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_Blueprint.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_BlueprintWorldTick.cpp`
   - `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_WorldTick.cpp`
8. 当前 `TESTING_GUIDE.md` 对 `SHARE_CLEAN` / `SHARE_FRESH` 仍允许“显式 `FAngelscriptEngineScope` 或其他局部清理”的示例写法，这解释了为什么大批文件虽然已经切到 `ASTEST_CREATE_ENGINE_*`，但还没有统一补齐 `BEGIN/END`。
9. `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` 仍依赖 `RequireRunningProductionEngine(...)`，Native / ASSDK / production / custom create 路径仍更适合作为“明确保留项”而不是机械补 `BEGIN/END` 的对象。
10. `Learning/Runtime/` 与 `Core/` 中还有少量文件不属于“59 个待统一生命周期的 ASTEST 文件”，因为它们分别落在 Native SDK、production engine、preprocessor-only 或 custom create path 上；这些文件必须显式归档为保留项/单独评估项，避免目录统计被误读成遗漏。

### 当前问题 → 对应解决方向

| 问题 | 当前症状 | 对应方向 |
| --- | --- | --- |
| 计划文本落后于仓库现状 | 旧计划仍把主问题写成“Validation 未迁移、70 个 direct helper 用户待处理” | 把现状改写成“ASTEST 已落地，主剩余工作是生命周期统一 + 旧 helper 语义尾项收尾” |
| 生命周期口径未统一 | 59 个 ASTEST 文件还没有统一 `BEGIN/END`；不同目录混用“无 BEGIN/END”“显式 scope”“局部 reset” | 先固定唯一权威规则，再按目录批量补齐或记录等价例外 |
| 旧 helper 尾项未闭环 | 还有 10 个文件直接调用或局部包装旧 helper 语义，继续阻碍 closeout | 把这 10 个文件单独列为收尾任务，不再分散在大范围 backlog 中 |
| 非宏化保留项边界不够清楚 | production / Native / custom create / 性能路径容易在批量迁移里被误替换 | 明确写成“保留项 / 单独评估项”，不计入本轮批量 `BEGIN/END` closeout |

## 影响范围

本次 closeout 涉及以下操作类型：

- **权威口径维护**：继续以 `ASTEST_*` 为唯一有效宏口径，清理计划与文档中的过时叙事。
- **生命周期标准化**：把 `ASTEST_CREATE_ENGINE_*` 调用补齐到匹配的 `ASTEST_BEGIN_* / ASTEST_END_*`，或在确有必要时保留明确记录的等价生命周期模式。
- **旧 helper 收尾**：清理对 `AcquireFreshSharedCloneEngine` / `AcquireCleanSharedCloneEngine` / `GetOrCreateSharedCloneEngine` 的直接调用与局部包装语义。
- **保留项分类**：把 Native / production / custom create / performance 路径从“未完成迁移”改成“明确保留或单独评估”。
- **验证与文档同步**：用标准 build/test 入口收口，并同步更新宏总结材料与计划状态。

按目录分组的剩余影响范围如下：

已完成的全链路 `BEGIN/END` 样板（13 个文件，不纳入待办）：

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
- `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptCompilerMacroValidationTests.cpp`

Shared/ 与文档收口（12 个文件）：

- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`：生命周期规则的最终权威定义
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`：共享 helper 边界对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`：helper API 边界与保留项说明
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`：helper 实现与 ASTEST 生命周期模型对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`：helper 自测保留旧 helper 语义或迁移为 ASTEST 的决策落地
- `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`：统一 `BEGIN/END` 权威规则
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md`：历史宏说明与当前口径对齐
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_ANALYSIS.txt`：历史分析材料与当前状态同步
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_MIGRATION_GUIDE.txt`：迁移说明改为 closeout 视角
- `MACRO_SYSTEM_README.md`：根目录宏说明与当前阶段同步
- `MACRO_VALIDATION_REPORT.md`：根目录验证总结同步剩余工作与保留项
- `MACRO_IMPLEMENTATION_SUMMARY.md`：根目录实现摘要同步剩余工作与保留项

Bindings/ 生命周期标准化（15 个文件）：

- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerCompareBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGlobalBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptIteratorBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp`

Fast / 工具链生命周期标准化（12 个文件）：

- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBytecodeTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptDataTypeTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptParserTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptScriptNodeTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptStructCppOpsTests.cpp`

Scenario / HotReload 生命周期标准化（19 个文件）：

- `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorInteractionTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorLifecycleTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorPropertyTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintSubclassActorTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintSubclassRuntimeTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptGCScenarioTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceDeclareTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Inheritance/AngelscriptInheritanceScenarioTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`

Learning/Runtime 生命周期标准化（13 个文件）：

- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningBlueprintSubclassTraceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningClassGenerationTraceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningComponentHierarchyTraceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningDelegateBridgeTraceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningGCTraceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningInterfaceDispatchTraceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningReloadAndClassAnalysisTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningScriptClassToBlueprintTraceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningUEBridgeTraceTests.cpp`

旧 helper 尾项（10 个文件，作为跨桶 overlay 单独跟踪，可与生命周期标准化目录重叠）：

- `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptScriptSpawnedActorOverrideTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateScenarioTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_Blueprint.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_BlueprintWorldTick.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_WorldTick.cpp`

明确排除 / 单独评估（不纳入本轮直接 `BEGIN/END` 收口）：

- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp`：production engine 路径
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`：production engine parity 路径
- `Plugins/Angelscript/Source/AngelscriptTest/Native/*`：Native / ASSDK 测试层
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Native/*`：Native learning trace 路径
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningExecutionTraceTests.cpp`：Native SDK 执行链路 trace
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`：preprocessor-only trace，不创建测试引擎
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningRestoreAndBytecodePersistenceTests.cpp`：custom isolated clone / bytecode restore 路径
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`：自定义 create path
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp`：性能 / 隔离语义优先
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp`：自定义 create path

## 分阶段执行计划

### Phase 0：已完成的口径收口

- [x] **P0.1** 固定 `ASTEST_*` 为当前唯一权威宏体系
  - 当前权威入口已经由 `Shared/AngelscriptTestMacros.h` 与 `TESTING_GUIDE.md` 承担，Validation 也已改为围绕 `ASTEST_*` 自身验证。
  - 本阶段不再作为待执行事项保留，而是作为已完成前置事实写入计划，避免后续执行者误以为仍需重新设计宏 API。
- [x] **P0.1** 📦 Git 提交：既有多次提交已完成；最近一次汇总提交：`1cd84bc [Test/Automation] Refactor: sync ASTEST migration with validation workflows`

- [x] **P0.2** 同步 Validation 与历史宏说明到当前口径
  - `Validation/AngelscriptMacroValidationTests.cpp` 与 `Validation/AngelscriptCompilerMacroValidationTests.cpp` 已迁移到 ASTEST 入口，历史宏说明文档也已转为“弃用 / 历史设计”口径。
  - 本阶段后续只需在 closeout 时维护表述一致性，不再把 Validation 迁移本身当成未完成项。
- [x] **P0.2** 📦 Git 提交：既有多次提交已完成；最近一次汇总提交：`1cd84bc [Test/Automation] Refactor: sync ASTEST migration with validation workflows`

- [x] **P0.3** 铺开第一轮 `ASTEST_CREATE_ENGINE_*` 覆盖
  - 当前已有 72 个 `.cpp` 文件切到 `ASTEST_CREATE_ENGINE_*`，说明“引擎创建宏已大面积落地”已经是既成事实。
  - 本计划后续不再把“是否迁到 ASTEST_CREATE_ENGINE_*”当作主线，而是把焦点切到生命周期标准化与旧 helper 语义尾项收尾。
- [x] **P0.3** 📦 Git 提交：既有多次提交已完成；最近一次汇总提交：`1cd84bc [Test/Automation] Refactor: sync ASTEST migration with validation workflows`

- [x] **P0.4** 锁定已完成的全链路 `BEGIN/END` 样板集
  - 当前已完成 `ASTEST_CREATE_ENGINE_* + ASTEST_BEGIN_* / ASTEST_END_*` 全链路接入的 13 个文件（11 个 `Angelscript/` 样板 + 2 个 `Validation/` 文件）应作为后续目录批量改造、代码评审与回归抽样的基线。
  - Phase 1~3 的目标是收口剩余 59 个 ASTEST 文件与 10 个旧 helper 语义尾项，不再把这 13 个样板重新纳入“待迁移”范围。
- [x] **P0.4** 📦 Git 提交：既有多次提交已完成；最近一次汇总提交：`1cd84bc [Test/Automation] Refactor: sync ASTEST migration with validation workflows`

### Phase 1：固定 `BEGIN/END` 生命周期唯一口径

- [ ] **P1.1** 固定 `BEGIN/END` 的唯一权威规则
  - 先以 `TESTING_GUIDE.md` 与 `Shared/AngelscriptTestMacros.h` 为准，明确 `FULL`、`SHARE`、`SHARE_CLEAN`、`SHARE_FRESH`、`CLONE`、`NATIVE` 各自允许的生命周期写法：哪些必须使用匹配的 `ASTEST_BEGIN_* / ASTEST_END_*`，哪些允许显式 `FAngelscriptEngineScope` 或局部 reset，哪些必须在计划中登记为例外。
  - 当前最大的执行风险不是“不会写宏”，而是不同目录各自发展出不同生命周期约定；本步骤的输出必须是可直接应用于批量改造的统一判定表。
  - 若需要调整 `TESTING_GUIDE.md` 示例或 `AngelscriptTestMacros.h` 中的注释口径，必须在这一阶段先落定，再进入批量补齐。
- [ ] **P1.1** 📦 Git 提交：`[Test/Macro] Docs: define canonical BEGIN END lifecycle rules for ASTEST`

- [ ] **P1.2** 收口 `Shared/` helper 自测与支持层边界
  - `Shared/AngelscriptTestEngineHelperTests.cpp`、`Examples/AngelscriptScriptExampleTestSupport.cpp` 以及 `Shared/AngelscriptTestEngineHelper.*` 是判断“旧 helper 是否仍需要暴露”的边界文件；它们不能继续作为模糊的历史残留存在。
  - 对 helper 自测，应明确区分“验证 helper 自身行为，因此保留旧 helper 语义”与“业务测试仍可迁到 ASTEST 生命周期”这两类场景，避免后续误把 helper 自测也机械替换掉。
  - 本步骤结束后，`Shared/` 层应能支撑后续目录批量接入 `BEGIN/END`，而不是继续制造新的局部惯例。
- [ ] **P1.2** 📦 Git 提交：`[Test/Macro] Refactor: clarify shared helper boundaries before lifecycle closeout`

### Phase 2：批量补齐生命周期接入

- [ ] **P2.1** 补齐 `Bindings/` 与 Fast / 工具链目录的 `BEGIN/END`
  - 处理 `Bindings/` 的 15 个文件，以及 `Core/`、`Compiler/`、`FileSystem/`、`ClassGenerator/`、`Internals/` 中尚未统一生命周期的 12 个文件。
  - 目标不是机械地往每个文件里塞宏，而是按 `P1.1` 给出的唯一口径完成统一：适合 `ASTEST_BEGIN_SHARE / ASTEST_END_SHARE` 的一律补齐，确实必须保留显式 scope 的要写成一致模式并在计划或 guide 中有依据。
  - `Core/AngelscriptEngineCoreTests.cpp`、`FileSystem/AngelscriptFileSystemTests.cpp`、`ClassGenerator/AngelscriptScriptClassCreationTests.cpp` 等文件要特别注意保留 create/destroy 与 reset 语义，不允许为了“看起来统一”削弱原有测试意图。
  - 本阶段回归重点为 `Angelscript.TestModule.Bindings.*` 与 `AngelscriptFast`。
- [ ] **P2.1** 📦 Git 提交：`[Test/Macro] Refactor: standardize BEGIN END usage for bindings and fast tests`

- [ ] **P2.2** 收口 Scenario / HotReload / `Learning/Runtime/` 的生命周期写法
  - 处理 `Actor/`、`Blueprint/`、`Component/`、`GC/`、`HotReload/`、`Interface/`、`Inheritance/`、`Subsystem/` 这 19 个文件，以及 `Learning/Runtime/` 这 13 个文件。
  - 这批文件的问题不是“没有 ASTEST_CREATE_ENGINE_*”，而是生命周期管理仍不一致：有的依赖共享引擎持久状态，有的依赖显式 scope，有的混合 local reset 与 reload 顺序；补齐前必须按场景语义逐类落位。
  - `HotReload/` 目录要保持 reload、property preservation、performance、spawner 初始化等语义不变；`Learning/Runtime/` 要保留教学 trace 的可读性，不为了抽象统一而牺牲讲解节奏。
  - 本阶段回归重点为 `Angelscript.TestModule.Actor.*`、`Blueprint.*`、`Component.*`、`GC.*`、`Interface.*`、`Subsystem.*`、`HotReload.*` 与 `Learning.Runtime.*`。
- [ ] **P2.2** 📦 Git 提交：`[Test/Macro] Refactor: standardize lifecycle patterns for scenario hot reload and learning tests`

### Phase 3：清理旧 helper 语义尾项

- [ ] **P3.1** 收口剩余 10 个旧 helper 语义尾项文件
  - 这 10 个文件是当前旧 helper 语义的明确尾项，必须逐个判断：是迁到 ASTEST 生命周期、拆成 helper 自测、还是作为模板 / 支持层保留并明确写清原因。
  - 该清单是一个跨目录的 overlay，不要求与 `P2.1` / `P2.2` 的生命周期目录桶互斥；像 `ClassGenerator/AngelscriptScriptClassCreationTests.cpp` 这类文件既可能属于生命周期标准化对象，也可能属于旧 helper 尾项收尾对象。
  - `Template/*.cpp` 与 `Examples/AngelscriptScriptExampleTestSupport.cpp` 的优先级很高，因为它们会继续影响未来新增测试的写法；如果这些文件继续保留旧 helper，后续新 case 仍会复制旧模式。
  - `Preprocessor/AngelscriptPreprocessorTests.cpp`、`ClassGenerator/ClassGeneratorTests.cpp` 与 `Delegate/AngelscriptDelegateScenarioTests.cpp` 则需要按真实语义判断是否适合迁到共享 ASTEST 生命周期，而不是按目录简单归类。
  - 本阶段完成后，旧 helper 语义不应再以“以后再看”的形式存在；要么迁完，要么写成明确保留项。
- [ ] **P3.1** 📦 Git 提交：`[Test/Macro] Refactor: eliminate remaining shared engine helper semantic tail items`

### Phase 4：保留项定稿并完成验证收口

- [ ] **P4.1** 标记不宏化保留项与单独评估项
  - `AngelscriptNativeScriptHotReloadTests.cpp`、`Core/AngelscriptEngineParityTests.cpp`、`Native/*`、`Learning/Native/*`、`Learning/Runtime/AngelscriptLearningExecutionTraceTests.cpp`、`Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`、`Learning/Runtime/AngelscriptLearningRestoreAndBytecodePersistenceTests.cpp`、`Core/AngelscriptBindConfigTests.cpp`、`Core/AngelscriptEnginePerformanceTests.cpp`、`Internals/AngelscriptRestoreTests.cpp` 不能继续挂在模糊 backlog 里。
  - 要么给出保留原因（production engine、ASSDK adapter、多引擎、性能语义、自定义 create path），要么明确再开 sibling plan；不能既不迁移也不解释。
  - `MACRO_SYSTEM_README.md`、`MACRO_VALIDATION_REPORT.md`、`MACRO_IMPLEMENTATION_SUMMARY.md` 与本计划必须同步反映这份结论，避免后续执行者误把这些路径视为本轮未完成项。
- [ ] **P4.1** 📦 Git 提交：`[Test/Macro] Docs: classify non macroized engine paths and exclusions`

- [ ] **P4.2** 用标准入口完成 build/test 收口
  - 先读取根目录 `AgentConfig.ini`，然后只通过 `Tools/RunBuild.ps1` 与 `Tools/RunTests.ps1` / `Tools/RunTestSuite.ps1` 执行验证。
  - 先做针对性回归：Validation、Shared.EngineHelper、Bindings、HotReload、Learning.Runtime，再用 `AngelscriptFast` 与 `AngelscriptScenario` group 做整体收口。
  - 若仍有 Native / production / performance 路径未迁移，但它们已被明确标记为保留项，则不应阻塞本计划 closeout；但必须在计划与总结文档里留下清晰结论。
- [ ] **P4.2** 📦 Git 提交：`[Test/Macro] Chore: close out lifecycle migration with verified groups`

## 验收标准

1. `Shared/AngelscriptTestMacros.h` 与 `TESTING_GUIDE.md` 持续作为唯一权威宏口径，仓库不再把 `ANGELSCRIPT_TEST` / `ANGELSCRIPT_ISOLATED_TEST` 描述成当前接口。
2. 所有纳入本计划批量改造范围的 ASTEST 文件，都满足下列二选一之一：
   - 使用与引擎创建宏匹配的 `ASTEST_BEGIN_* / ASTEST_END_*`；
   - 使用在 `P1.1` 明确定义并记录的等价生命周期模式，且不再是目录各自为政的临时写法。
3. 旧 helper 语义尾项收缩到零，或只保留在明确记录原因的 helper 自测 / 保留项文件中；仓库不再存在“语义未知、以后再看”的旧 helper 用户。
4. `MACRO_SYSTEM_README.md`、`MACRO_VALIDATION_REPORT.md`、`MACRO_IMPLEMENTATION_SUMMARY.md` 与本计划对剩余工作、保留项、closeout 条件的表述一致。
5. 至少完成以下标准验证并保留结果：
    - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -TimeoutMs 180000 -- -NoXGE`
    - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Validation." -TimeoutMs 600000`
    - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Shared.EngineHelper." -TimeoutMs 600000`
    - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Bindings." -TimeoutMs 600000`
    - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.HotReload." -TimeoutMs 600000`
    - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Learning.Runtime." -TimeoutMs 600000`
    - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -Group AngelscriptFast -TimeoutMs 600000`
    - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -Group AngelscriptScenario -TimeoutMs 600000`

## 风险与注意事项

### 风险

1. **`SHARE_CLEAN` / `SHARE_FRESH` 与 `BEGIN/END` 的关系尚未正式定稿**
   - 当前指南允许显式 scope 或局部 reset，这让批量补齐 `BEGIN/END` 很容易走向“表面统一、实际语义漂移”。
   - **缓解**：先完成 `P1.1`，再进行目录级批量改造；不得在口径未定时直接机械补宏。

2. **HotReload 与 scenario 测试容易被过度抽象**
   - 如果为了统一写法而隐藏关键的 reload、teardown、reset 顺序，文件会变短，但测试语义会变弱。
   - **缓解**：允许 HotReload / scenario 文件保留必要的显式局部逻辑，但必须符合 `P1.1` 给出的统一例外规则。

3. **helper 自测与业务测试边界容易混淆**
   - `Shared/AngelscriptTestEngineHelperTests.cpp` 这类文件本身就需要验证 helper 行为，不能与普通业务回归用同一把尺子强行处理。
   - **缓解**：在 `P1.2` 里先区分“helper 自测保留旧 helper 语义”与“业务测试应迁到 ASTEST 生命周期”的场景。

4. **模板与 Example 支持层若不收口，会继续扩散旧模式**
   - 即使业务测试逐步迁完，只要 `Template/*.cpp` 或 `Examples/*` 继续展示旧 helper，后续新增测试仍会重复历史写法。
   - **缓解**：把模板与示例支持层纳入 `P3.1` 的明确尾项，而不是放到“顺手再改”。

### 已知行为变化

1. **大量测试文件会从“只有 `ASTEST_CREATE_ENGINE_*`”转为“显式生命周期入口”**
   - 影响范围：`Bindings/`、Fast / 工具链目录、Scenario / HotReload、`Learning/Runtime/`
   - 说明：这次 closeout 的核心不是新增引擎创建宏，而是统一生命周期模式；文件形态会变，但 Automation 前缀、断言意图与测试主题不应改变。

2. **模板与示例支持层将从“兼容旧 helper”转为“展示当前 ASTEST 正式写法”**
   - 影响文件：`Template_Blueprint.cpp`、`Template_BlueprintWorldTick.cpp`、`Template_WorldTick.cpp`、`Examples/AngelscriptScriptExampleTestSupport.cpp`
   - 说明：这些文件未来会继续被复制和参考，因此它们的风格变化属于预期且必要的行为变化。

3. **根目录宏总结材料会从“迁移进行中”改写为“closeout 与保留项并行”**
   - 影响文件：`MACRO_SYSTEM_README.md`、`MACRO_VALIDATION_REPORT.md`、`MACRO_IMPLEMENTATION_SUMMARY.md`
   - 说明：这些文件的职责不再是叙述第一轮宏设计，而是准确反映当前 closeout 状态、剩余生命周期工作和保留项边界。

## 依赖关系

```text
Phase 0 已完成的 ASTEST 权威口径与 Validation 收口
  -> Phase 1 固定 BEGIN/END 唯一生命周期规则
  -> Phase 2 按目录批量补齐生命周期接入
  -> Phase 3 清理旧 helper 语义尾项
  -> Phase 4 标记保留项并完成 build/test closeout
```

## 参考文档

| 文档 | 用途 |
| --- | --- |
| `Documents/Plans/Plan.md` | Plan 结构、checkbox 规则、影响范围章节要求 |
| `Documents/Guides/Build.md` | 标准构建入口与超时规则 |
| `Documents/Guides/Test.md` | 标准测试入口与回归命令 |
| `Documents/Guides/TestConventions.md` | 测试层级、目录与 Automation 前缀规则 |
| `Plugins/Angelscript/AGENTS.md` | `AngelscriptTest` 模块的边界与命名约束 |
| `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` | 当前宏体系说明与生命周期示例 |
