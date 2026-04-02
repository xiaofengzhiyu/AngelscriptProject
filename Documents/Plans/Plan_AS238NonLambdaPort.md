# AngelScript 2.38 非 Lambda 语言特性与优化迁移计划

## 背景与目标

### 背景

当前 `Plugins/Angelscript` 内嵌的 AngelScript 公共头版本仍标记为 `2.33.0 WIP`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h`。

但当前仓库并不是一个“纯 2.33 原样”的第三方快照，而是一个已经吸收了部分 `2.37/2.38` 公共能力面、同时叠加大量 `[UE++]` 改造的混合基线。与本次“非 lambda 迁移”直接相关的现状如下：

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h` 已经暴露 `asEP_ALWAYS_IMPL_DEFAULT_COPY`、`asEP_ALWAYS_IMPL_DEFAULT_COPY_CONSTRUCT`、`asEP_MEMBER_INIT_MODE`、`asEP_BOOL_CONVERSION_MODE`、`asEP_FOREACH_SUPPORT` 等 `2.38` 公共属性位，但版本号仍保持 `23300`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 已在初始化阶段设置 `asEP_ALWAYS_IMPL_DEFAULT_COPY = 1`、`asEP_ALWAYS_IMPL_DEFAULT_COPY_CONSTRUCT = 1`、`asEP_MEMBER_INIT_MODE = 0`，说明当前插件已经把部分 `2.37/2.38` 行为当作兼容基线使用。
- `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp` 已存在 `[UE++]` 的 `CompileForeachStatement(...)` 降级实现，说明 `foreach` 不是“完全没有”，而是“已接了一段本地实现，但是否等价于上游 2.38 仍未完成审计”。
- `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h/.cpp` 已存在 `templateSubTypes` 数据结构及引用计数处理，说明模板函数/模板子类型已经留下半移植痕迹；但当前公共头和实现中仍未找到 `asFUNC_TEMPLATE`、`GetSubTypeCount()`、`GetSubTypeId()`、`GetSubType()` 的完整闭环。
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` 当前仍把 `funcdef/function pointer` 与 `template` 语法视为负例能力边界，这与上游 `2.38` 的模板函数、匿名函数外围能力、泛型调用路径已经发生直接冲突。
- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` 已经把一部分 `2.38` 公共枚举值、对象标志位和 engine property round-trip 固化为兼容回归，说明当前仓库已经接受“按主题分阶段吸收上游 2.38 能力”，而不是一次性整包替换 ThirdParty。

同时，上游 `Reference/angelscript-v2.38.0` 已给出足够明确的能力基线：

- `Reference/angelscript-v2.38.0/sdk/docs/doxygen/source/doc_versions.h`
  - 明确记录 `2.38.0` 的重点语言特性：`foreach`、`variadic arguments`、`template functions`
- `Reference/angelscript-v2.38.0/sdk/docs/articles/changes2.html`
  - 明确记录 `2.38.0` 的脚本语言与库层改动：`using namespace`、contextual conversion to bool、template function registration、variadic generic calling convention、`asEP_MEMBER_INIT_MODE`、`asEP_BOOL_CONVERSION_MODE`、`asEP_FOREACH_SUPPORT`
- 上游 feature tests：
  - `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_template.cpp`
  - `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_foreach.cpp`
  - `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_namespace.cpp`
  - `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_exception.cpp`

当前仓库已经有专门的 `Documents/Plans/Plan_AS238LambdaPort.md` 负责 lambda / anonymous function 主题，本计划必须与其边界严格分离，避免把非 lambda 路线重新揉回 lambda 计划里。

### 目标

本计划的目标不是“整体把当前 ThirdParty 一步升级到完整 `2.38.0`”，而是：

1. 在当前 `Plugins/Angelscript` 基线中，**分主题迁移 AngelScript 2.38 中除 lambda 外的高价值语言特性与优化**。
2. 允许修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/`，但坚持“最小必要移植 + 本地适配”，不做机械整包替换。
3. 把当前已经暴露出来但尚未闭环的 `2.37/2.38` 公共表面（engine property、模板子类型数据、foreach lowering）收敛为真正可验证、可维护的能力。
4. 为 parser / compiler / language / pipeline / restore / hot-reload 六层补齐单元测试与回归测试。
5. 给后续真正的“完整 2.38 对齐”保留清晰边界：本次只迁移非 lambda 的高价值主题，不把 add-on、JIT 全链路、宿主项目改造等大范围事项一起卷入。

## 范围与边界

- **纳入范围**
  - `template functions` 的公共 API、注册链路、编译链路与模板子类型元数据闭环
  - `variadic arguments` 在 generic calling convention 与脚本调用路径上的最小可用支持
  - `using namespace` 的 parser / builder / symbol lookup 语义闭环
  - `contextual conversion to bool` 的编译语义闭环
  - `foreach` 从当前 `[UE++]` lowering 到上游 `2.38` 语义的审计、补齐与回归
  - `asEP_MEMBER_INIT_MODE`、`asEP_ALWAYS_IMPL_DEFAULT_COPY`、`asEP_ALWAYS_IMPL_DEFAULT_COPY_CONSTRUCT` 对应的 compiler 行为核对与补齐
  - 与以上主题直接相关的 restore / save-load / hot-reload / runtime 稳定性修复
  - `Plugins/Angelscript/Source/AngelscriptTest/` 下的 internals / language / pipeline / restore / hot-reload 测试
  - `Documents/Guides/Test.md` 与必要的知识文档同步
- **不纳入范围**
  - `lambda / anonymous function` 主题（由 `Documents/Plans/Plan_AS238LambdaPort.md` 负责）
  - 整体把 `ANGELSCRIPT_VERSION` 直接翻到 `23800` 并宣称完成完整 2.38 升级
  - `StaticJIT` / `DebugServer` / IDE 工作流的完整重做
  - 与核心语言本体无关的 add-on 全量迁移
  - 宿主工程 `Source/AngelscriptProject/` 的业务功能扩展

## 当前事实状态快照

### 语言与 ThirdParty 事实

- 当前公共版本号：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h`
  - `ANGELSCRIPT_VERSION = 23300`
  - `ANGELSCRIPT_VERSION_STRING = "2.33.0 WIP"`
- 当前已暴露 `2.38` 属性面：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h`
  - `asEP_ALWAYS_IMPL_DEFAULT_COPY = 36`
  - `asEP_ALWAYS_IMPL_DEFAULT_COPY_CONSTRUCT = 37`
  - `asEP_MEMBER_INIT_MODE = 38`
  - `asEP_BOOL_CONVERSION_MODE = 39`
  - `asEP_FOREACH_SUPPORT = 40`
- 当前 engine property 存储/读取：`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptengine.cpp`
  - 已支持 set/get `memberInitMode`、`boolConversionMode`、`foreachSupport`
  - 但仅从当前 grep 结果无法证明 compiler 语义已经完整消费这些属性
- 当前 foreach 实现：`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`
  - 已存在 `CompileForeachStatement(...)`
  - 通过生成 `opForBegin / opForEnd / opForNext / opForValue / opForKey` 的 lowering 代码实现当前分支的 `foreach`
- 当前模板子类型痕迹：`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h/.cpp`
  - 已有 `templateSubTypes`
  - 已有 addref/release 处理
  - 但未找到 `asFUNC_TEMPLATE` 与公共 `GetSubType*` API
- 当前命名空间能力：`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptengine.cpp`
  - 已有 `SetDefaultNamespace()` / `GetDefaultNamespace()`
  - 尚未证明已经具备上游 `using namespace` 导入语义

### 当前测试与能力边界事实

- `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`
  - `Functions.Pointer` 当前显式断言 `funcdef / function pointer` 语法应失败
  - `Functions.Template` 当前显式断言模板语法应失败
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptParserTests.cpp`
  - 已有 declaration / expression / control-flow / syntax-error 入口
  - 适合补 `using namespace`、`foreach`、variadic 参数列表、template function parser 测试
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp`
  - 已有 bytecode / scope / function call / type conversion 入口
  - 适合补 bool conversion、template function call、member init mode 测试
- `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp`
  - 适合补 template / variadic / namespace / foreach 的 save-load 回归
- `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`
  - 适合补 end-to-end compile pipeline 回归
- `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`
  - 适合补模板函数/命名空间/函数签名变化与热重载回归

### 上游参考锚点

| 主题 | 上游文档/测试 | 当前分支对应面 |
| --- | --- | --- |
| `template functions` | `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_template.cpp` | `as_scriptfunction.h/.cpp`、`as_scriptengine.cpp`、`AngelscriptFunctionTests.cpp` |
| `foreach` | `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_foreach.cpp` | `as_compiler.cpp`、`AngelscriptControlFlowTests.cpp` |
| `using namespace` | `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_namespace.cpp` | `as_parser.cpp`、`as_builder.cpp`、`as_scriptengine.cpp` |
| bool conversion | `Reference/angelscript-v2.38.0/sdk/docs/articles/changes2.html` | `as_scriptengine.cpp`、`AngelscriptCompilerTests.cpp` |
| member init/default copy | `Reference/angelscript-v2.38.0/sdk/docs/articles/changes2.html`、`doc_versions.h` | `Core/angelscript.h`、`AngelscriptEngine.cpp`、`as_compiler.cpp` |
| try/catch stack restore 等稳定性修复 | `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_exception.cpp` | `as_context.cpp`、`as_restore.cpp`、`AngelscriptRestoreTests.cpp` |

## 推荐技术路线

本 Plan 采用以下固定路线，而不是把选择留到实现时临场漂移：

1. **先做能力矩阵与公共表面审计，再做 feature port**。当前分支已经半吸收 `2.38`，如果不先厘清“哪些表面已存在、哪些语义未落地”，后续实现极易重复改动或误删 `[UE++]` 逻辑。
2. **按主题定点移植，不做整包替换**。本次优先主题固定为：`template functions`、`variadic`、`using namespace`、`bool conversion`、`foreach`、`member-init/default-copy`、与这些主题直接相关的优化/修复。
3. **parser / builder / compiler / runtime 分层推进**。语言特性先让 parser 能识别，再让 builder / compiler / runtime 正确消费，最后补 restore / hot-reload 回归。
4. **测试先行**。每个 Phase 中涉及语言行为变化的条目，都要先把失败测试补出来，再动 ThirdParty。
5. **保留本地 `[UE++]` 适配标记**。任何 ThirdParty 代码迁移都必须延续当前仓库的 `[UE++]` / `[UE--]` 标记习惯，避免后续再次变成黑盒补丁。

## 首批测试落点建议

为避免后续执行时再次临时决定“测试该落在哪个文件、叫什么名字”，本计划先固定第一批推荐落点：

| 主题 | 首批测试文件 | 建议测试名 |
| --- | --- | --- |
| `template functions` 公共 API | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp` | `Angelscript.TestModule.Angelscript.Upgrade.TemplateFunctionApi` |
| template function 编译/调用 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` | `Angelscript.TestModule.Internals.Compiler.TemplateFunctions` |
| variadic 参数 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` | `Angelscript.TestModule.Internals.Compiler.VariadicCalls` |
| `using namespace` 语法 | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptParserTests.cpp` | `Angelscript.TestModule.Internals.Parser.NamespaceImports` |
| `using namespace` pipeline | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` | `Angelscript.TestModule.Compiler.EndToEnd.NamespaceImports` |
| `foreach` | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` | `Angelscript.TestModule.Angelscript.ControlFlow.Foreach` |
| bool conversion | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` | `Angelscript.TestModule.Internals.Compiler.BoolConversionMode` |
| member-init/default-copy | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp` | `Angelscript.TestModule.Internals.Compiler.MemberInitAndDefaultCopy` |
| restore/save-load | `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp` | `Angelscript.TestModule.Internals.Restore.AS238NonLambda` |
| hot-reload | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` | `Angelscript.TestModule.HotReload.AS238NonLambdaFunctions` |

## 实施纪律

- 所有实现阶段默认遵循 **先写失败测试 → 再补实现 → 再跑目标回归** 的顺序。
- 若某一主题在实施中证明必须牵出完整 JIT / add-on / 全版本 ABI 升级，则应停止扩 scope，单独补写新计划，不在本计划内继续膨胀。
- 任何把现有负例改为正例的工作，都必须同步把“为什么旧负例不再成立”写进测试说明或文档，不允许静默翻转能力边界。

## 分阶段执行计划

### Phase 0：冻结主题范围与本地/上游对照矩阵

> 目标：把“这次到底迁移什么、哪些只是半移植、哪些明确排除”固定下来，避免后续实现边界不断漂移。

- [ ] **P0.1** 固化本次非 lambda 候选主题与排除范围
  - 背景信息：`Reference/angelscript-v2.38.0/sdk/docs/doxygen/source/doc_versions.h` 把 `2.38.0` 定义为 `foreach / variadic / template functions` 的正式落点；`Reference/angelscript-v2.38.0/sdk/docs/articles/changes2.html` 进一步把 `using namespace`、contextual bool conversion、member-init 兼容属性、template/variadic 相关接口补齐列为本版本重点。
  - 依据 `Reference/angelscript-v2.38.0/sdk/docs/doxygen/source/doc_versions.h` 与 `Reference/angelscript-v2.38.0/sdk/docs/articles/changes2.html`，将本次主题固定为：`template functions`、`variadic arguments`、`using namespace`、contextual bool conversion、`foreach`、member-init/default-copy 语义闭环、与这些主题直接相关的 runtime/restore 修复
  - 明确写入排除项：`lambda`、完整 `23800` 翻版、完整 JIT/StaticJIT 改造、无关 add-on 迁移
  - 落实方案：把所有候选项按 `必须迁移 / 条件迁移 / 显式排除` 三档写入文档正文；其中 `variadic` 要额外标记“依赖 P0.4 的编译管线与宿主注册面审计结果”，防止后续实现时无边界膨胀。
  - 验证方式：最终文档中每个主题都必须能对应到至少一个上游文档或上游测试锚点，且排除项必须能指向现有 sibling plan（lambda）或后续独立计划。
  - 依赖与注意：本项是后续所有 Phase 的边界基准；任何实施过程中新增的主题，如果无法归入这三档，必须先回写文档再继续。
- [ ] **P0.1** 📦 Git 提交：`[AS238] Docs: freeze non-lambda migration scope`

- [ ] **P0.2** 建立本地/上游文件对照清单
  - 背景信息：当前仓库不是“整洁的 2.33 vs 2.38 差异”，而是“2.33 版本号 + 2.38 表面 API + UE++ 私有改造”的混合体；如果不先固定本地/上游对照矩阵，任何后续 cherry-pick 都可能误伤 `[UE++]` 逻辑。
  - 主参考文件固定为：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_parser.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptengine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_restore.cpp`
    - `Reference/angelscript-v2.38.0/sdk/angelscript/source/*`
    - `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_template.cpp`
    - `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_foreach.cpp`
    - `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_namespace.cpp`
    - `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_exception.cpp`
  - 落实方案：在计划正文或附属知识文档中补一张统一矩阵，至少包含 `主题 / 上游文件 / 当前本地文件 / 当前状态(缺失|半移植|已落地待验证) / 首批测试文件` 五列；`foreach`、template、namespace、variadic 必须分别写出“现有半移植点”。
  - 验证方式：矩阵中的每个主题至少能落到 2 个本地路径，其中一个是 ThirdParty 源文件，另一个是测试或宿主接入路径。
  - 依赖与注意：该矩阵后续要给执行阶段复用，不能只写目录，必须写到具体文件名；若能确定函数名，应优先写函数名。
- [ ] **P0.2** 📦 Git 提交：`[AS238] Docs: add local-vs-upstream matrix for non-lambda port`

- [ ] **P0.3** 固定当前负例能力边界作为迁移门槛
  - 背景信息：`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` 目前仍把 `Functions.Pointer` 与 `Functions.Template` 当成失败边界；如果不先把这些旧门槛固定下来，后续实现很容易在“删掉旧负例”与“真正建立新正例”之间偷步。
  - 记录 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` 中当前 `Functions.Pointer` 与 `Functions.Template` 负例
  - 明确执行规则：这些负例后续若被翻正，必须同步补上新的正例与过渡说明，不能只删断言
  - 落实方案：在本计划中明确列出“哪些旧负例会被拆成新正例 + 新负例边界”，例如 template function 支持后，`Functions.Template` 不能整体删除，而应拆成“已支持注册模板函数正例”与“仍不支持的非目标模板语法负例”。
  - 验证方式：后续实施 PR 中，任何触碰这两个测试点的提交都必须同时包含新增正例或说明文档，不允许只修改期望字符串。
  - 依赖与注意：`Functions.Pointer` 同时与 lambda/function handle 路线相邻，若涉及 anonymous function，则必须回看 `Plan_AS238LambdaPort.md` 边界。
- [ ] **P0.3** 📦 Git 提交：`[AS238] Docs: record current negative capability gates`

- [ ] **P0.4** 前置审计预处理器、测试 helper 与编译管线入口
  - 背景信息：Oracle 评审已指出，本计划此前默认“源码会原样到达 ThirdParty parser/compiler”，但当前插件实际存在独立的预处理与注解编译路径；若这些入口先重写或过滤语法，单改 `as_parser.cpp`/`as_compiler.cpp` 不会真正让功能落地。
  - 审计但仅按需修改以下路径：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/PreprocessorTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`
  - 落实方案：
    - 先确认 `using namespace`、`foreach`、template function 调用语法、variadic 形参声明能否在 `CompileAnnotatedModuleFromMemory()` 与普通 `BuildModule()` 路径下原样通过
    - 记录预处理器只应剥离 `import`、宏/元数据，不应把新语法折叠或误判成 UE 注解
    - 如果 variadic 需要 generic host registration 支撑，则在本项里先确认是否存在必须联动的宿主注册封装，再决定 P2.3 是否继续实施或降级为条件项
  - 验证方式：
    - `PreprocessorTests.cpp` 至少新增一条“新语法原样保留”用例
    - `AngelscriptCompilerPipelineTests.cpp` 至少新增一条“新语法穿透 compile pipeline”用例
  - 依赖与注意：本项必须先于 P2/P3/P4 的语言特性实现完成，否则后续 ThirdParty 改动可能被前置管线直接吞掉。
- [ ] **P0.4** 📦 Git 提交：`[AS238] Test: audit preprocessor and compile-pipeline entrypoints`

### Phase 1：公共表面与 engine-property 兼容面闭环

> 目标：先把当前“已经露出来但还没闭环”的 2.37/2.38 公共表面整理干净，再进入更深的 parser/compiler 迁移。

- [ ] **P1.1** 审计 `angelscript.h` 与 `as_scriptfunction` 的模板函数公共表面
  - 背景信息：当前本地 `as_scriptfunction.h/.cpp` 已经拥有 `templateSubTypes` 及其 addref/release 逻辑，`as_restore.cpp` 也已有 `registeredTemplateSubTypes` 兼容别名，但公共头 `angelscript.h` 仍缺 `asFUNC_TEMPLATE` 与 `GetSubType*` 系列接口，说明当前状态是“私有数据存在，公共 API 未闭环”。
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h`
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`
  - 必要时同步修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp`
  - 对照上游 `2.38`，确认并补齐以下缺口：
    - `asFUNC_TEMPLATE`
    - `asIScriptFunction::GetSubTypeCount()`
    - `asIScriptFunction::GetSubTypeId()`
    - `asIScriptFunction::GetSubType()`
  - 若当前分支已有等价私有实现，但公共头未暴露，则优先补公共表面，而不是重新设计私有数据结构
  - 首批验证：在 `AngelscriptUpgradeCompatibilityTests.cpp` 中新增“模板函数 API 存在性 + 子类型枚举可读”断言
  - 落实方案：
    - 先补 enum / interface / virtual method 声明，再补 `asCScriptFunction` 对这些接口的具体实现
    - `GetDeclaration()` 路径要同步核对，确保模板子类型能进入声明串，不只是在内部数据里存在
    - 若 `asIScriptFunction` 的新增接口会影响 ABI 注释或 wrapper 头，需在文档中明确这是“局部 API 对齐”，不是完整版本号升级
  - 验证方式：
    - `AngelscriptUpgradeCompatibilityTests.cpp` 断言 `asFUNC_TEMPLATE` 枚举位存在
    - 编译期确认 `GetSubTypeCount/GetSubTypeId/GetSubType` 可被调用
    - 对空模板子类型与多子类型场景分别断言返回值稳定
  - 依赖与注意：先完成 P0.4，确认测试 helper/compile path 不会吞掉后续 template 调用语法，再进入 P2.*。
- [ ] **P1.1** 📦 Git 提交：`[ThirdParty/AS238] Feat: expose template function API surface`

- [ ] **P1.2** 审计 `as_scriptengine.cpp` 与 `AngelscriptEngine.cpp` 的 property 语义闭环
  - 背景信息：当前 `as_scriptengine.cpp` 已能存取 `memberInitMode`、`boolConversionMode`、`foreachSupport`，而 `AngelscriptEngine.cpp` 又把 default copy / copy construct / member init mode 直接强制到 APV2 兼容值。这说明“属性表面”已对齐，但还不能证明 compiler/runtime 真的按照这些值工作。
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptengine.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
  - 核对 `asEP_MEMBER_INIT_MODE`、`asEP_BOOL_CONVERSION_MODE`、`asEP_FOREACH_SUPPORT`、`asEP_ALWAYS_IMPL_DEFAULT_COPY`、`asEP_ALWAYS_IMPL_DEFAULT_COPY_CONSTRUCT` 的默认值、允许取值和插件初始化值
  - 明确哪些值是“上游默认”，哪些值是“当前 APV2/UE++ 兼容钉死值”
  - 落实方案：
    - 在文档里给每个 property 补“上游默认 / 当前插件默认 / 预期消费位置”三列说明
    - 若某 property 当前只是 set/get 存在、尚无消费点，明确标注为“表面已暴露，语义待 P4 完成”
    - 对 `asEP_FOREACH_SUPPORT` 额外说明当前默认值为什么仍保持开启，以及兼容关闭时的目标行为
  - 验证方式：
    - `AngelscriptUpgradeCompatibilityTests.cpp` round-trip
    - grep/代码审计确认 compiler 中真实消费位置
    - 若某属性未被消费，必须在计划中写出“尚缺消费点”的事实，而不是模糊写成已支持
  - 依赖与注意：本项是 P4.2 / P4.3 的前置审计，不要求此时就完成语义实现，但必须把“哪些只是表面属性”钉清楚。
- [ ] **P1.2** 📦 Git 提交：`[ThirdParty/AS238] Chore: align engine property defaults and compatibility notes`

- [ ] **P1.3** 扩展公共兼容回归测试
  - 背景信息：当前 `AngelscriptUpgradeCompatibilityTests.cpp` 已经承担了“2.38 公共表面 parity 守门员”的角色，最适合继续承接模板 API、engine property、对象标志位的回归，而不是把这些低层检查分散到行为测试中。
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp`
  - 在现有 property / flag parity 基础上，新增模板函数公共 API 存在性与 round-trip 覆盖
  - 明确本阶段仍不改 `ANGELSCRIPT_VERSION`，避免过早宣称全量升级完成
  - 落实方案：
    - 补齐 `asFUNC_TEMPLATE`、`GetSubType*`、property id 与默认值断言
    - 对“当前版本号仍为 23300，但 2.38 表面 API 已局部存在”的事实新增显式说明断言，防止后续有人误把版本号当能力唯一判断依据
  - 验证方式：该测试组在不依赖模板语法真正可编译的前提下也要能运行，通过后才能进入 P2/P4 的行为语义迁移。
  - 依赖与注意：本项要尽量保持“表面检查”属性，不要提前把真正的行为逻辑塞进 compatibility test。
- [ ] **P1.3** 📦 Git 提交：`[Test/AS238] Test: extend public compatibility parity coverage`

### Phase 2：模板函数与 variadic 调用链路移植

> 目标：把当前已经有 `templateSubTypes` 痕迹但没有完整闭环的模板函数能力，以及 2.38 的 variadic generic 调用能力，真正接通到可编译、可执行、可回归的状态。

- [ ] **P2.1** 对齐模板函数注册面
  - 背景信息：官方 `2.38` 明确把“用 generic calling convention 注册 template functions”列为 Library 改动；当前仓库已有 `templateSubTypes` 数据与引用计数，但还没有证据表明 `RegisterGlobalFunction` / `RegisterObjectMethod` 真正识别 template function。
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptengine.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp`
  - 对齐上游 `2.38` 的 `RegisterGlobalFunction()` / `RegisterObjectMethod()` 对模板函数的注册逻辑
  - 接通 `templateSubTypes` 的签名、声明串、引用计数与生命周期处理
  - 预期结果：注册后 `GetDeclaration()` 能稳定反映模板子类型；函数释放/模块丢弃时不发生 template subtype 悬挂引用
  - 落实方案：
    - 先写失败测试：在 `Internals/AngelscriptCompilerTests.cpp` 或独立 helper 中注册最小 generic template function，确认当前分支无法正确识别/枚举 subtype
    - 再对齐 `funcType`、模板子类型存储、声明串拼装、模块丢弃时的引用释放
    - 如果对象方法模板函数与全局模板函数共用实现路径，优先复用统一 helper，避免复制一套注册逻辑
  - 验证方式：
    - 注册后可通过 `GetDeclaration()` 和 `GetSubType*` 读到模板子类型
    - 模块 discard / engine shutdown 后不出现 template subtype 泄漏或悬挂引用
  - 依赖与注意：本项只负责“注册面”，不要求此时就能从脚本里实例化调用；脚本实例化放到 P2.2。
- [ ] **P2.1** 📦 Git 提交：`[ThirdParty/AS238] Feat: wire template function registration and metadata`

- [ ] **P2.2** 对齐 template function 的编译与调用链路
  - 背景信息：上游 `2.38` 的关键不是“引擎里存了 templateSubTypes”，而是“脚本可以实例化已注册模板函数并调用”；当前本地 `Functions.Template` 还是负例，这正是需要被翻转的主能力边界。
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_parser.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`
  - 目标不是引入一套新的模板系统，而是让上游 `2.38` 的“已注册模板函数可实例化并可调用”成立
  - 必须覆盖不同 subtype 个数、不同返回类型、对象方法模板函数三类路径
  - 首批失败测试先落到 `Internals/AngelscriptCompilerTests.cpp`，然后再补 `AngelscriptFunctionTests.cpp` 的正向行为用例
  - 落实方案：
    - 先拆旧 `Functions.Template` 负例，改成“非目标模板语法仍失败 + 注册模板函数正例待通过”的双边界结构
    - 在 parser/builder 中接通模板函数调用语法的最小识别，再让 compiler 完成 subtype 推导或显式 subtype 消费
    - 先覆盖最小 `T -> T` 或 `T,T -> bool` 模式，再扩到对象方法模板函数，避免一开始把所有模板形态全打通
  - 验证方式：
    - `Internals.Compiler.TemplateFunctions` 先红后绿
    - `AngelscriptFunctionTests.cpp` 能执行至少一个注册模板函数正例
    - 缺失 subtype 的错误路径仍能给出稳定诊断
  - 依赖与注意：本项必须晚于 P0.3，因为不先拆旧负例，后续很难证明“新能力是主动放开而不是误删断言”。
- [ ] **P2.2** 📦 Git 提交：`[ThirdParty/AS238] Feat: compile and invoke registered template functions`

- [ ] **P2.3** 接通 variadic 参数的最小能力闭环
  - 背景信息：官方 `2.38` 把 variadic 明确定义为“generic calling convention 支持的注册能力”，并且 changelog 里连续出现 stack pointer、constructor/factory、save/load 等多条 variadic 相关 bugfix，说明这项能力天然跨 parser / compiler / context / restore 多层。
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_parser.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`
  - 以 `2.38` 的 generic calling convention 为基线，只补当前插件真正需要的 variadic 注册、参数解析与执行栈处理
  - 不在本阶段顺带迁移无关 add-on 对 variadic 的扩展 API
  - 预期结果：variadic 注册函数可通过 generic calling convention 被调用，且参数栈在单次执行、重复执行、保存/恢复后都不破坏上下文状态
  - 落实方案：
    - 先写失败测试，证明 `...` 形参当前无法通过 parser/compiler 或 generic 调用路径
    - 再把 parser 规则限制为“仅最后一个参数允许 variadic”，避免过度扩展
    - 在 `as_context.cpp` / generic 调用层明确记录参数打包、返回对象、constructor/factory 这三条最容易炸栈的路径
    - 若 P0.4 证明当前宿主绑定层没有必要或没有现成封装来暴露 variadic generic registration，则将本项降级为“仅 ThirdParty 能力闭环 + 本地 helper 验证”，不强行扩到全插件 API
  - 验证方式：
    - `AngelscriptCompilerTests.cpp` 中新增 `VariadicCalls`
    - `AngelscriptRestoreTests.cpp` 中至少覆盖一次 save/load 后调用 variadic 的回归
    - constructor/factory 相关特殊路径至少覆盖一条失败/成功用例
  - 依赖与注意：这是本计划中最容易超 scope 的条目；若宿主注册面成本显著超出预期，应优先保证 template/namespace/foreach 主题，不让 variadic 拖垮全计划。
- [ ] **P2.3** 📦 Git 提交：`[ThirdParty/AS238] Feat: port minimal variadic generic call path`

### Phase 3：`using namespace` 与命名空间查找语义对齐

> 目标：把当前只有 `SetDefaultNamespace()` 的基础能力，提升为接近上游 `2.38` 的 `using namespace` 语义闭环。

- [ ] **P3.1** 补齐 parser 对 `using namespace` 的语法识别
  - 背景信息：explore 结果显示当前本地 `as_scriptnode.h` 里有 `snNamespace`，但没有 `snUsing` 或等价节点，说明 `using namespace` 在 parser 层仍是实缺，而不只是 builder/lookup 没消费。
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_parser.cpp`
  - 必须覆盖：
    - 文件级 `using namespace X;`
    - 语句块级 `using namespace X;`
    - 多层嵌套命名空间与局部可见性退出
  - 对照 `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_namespace.cpp`
  - 首批失败测试先落到 `Internals/AngelscriptParserTests.cpp`，明确 AST 能识别 namespace import 语句
  - 落实方案：
    - 先补 token / node / parse entry，不要一上来把符号查找也混进去
    - 让 AST 层能区分 `namespace Foo {}` 与 `using namespace Foo;` 两类语句，后续 builder 才能稳定消费
    - 若 parser 中已有相邻的 namespace 语法 helper，优先沿用现有结构，避免再创造一套并行语法入口
  - 验证方式：
    - `Internals.Parser.NamespaceImports` 能区分 declaration node、namespace node、using/import node
    - 对文件级与块级导入分别建树成功
  - 依赖与注意：本项只负责“识别”，不承担冲突解析；冲突解析在 P3.2。
- [ ] **P3.1** 📦 Git 提交：`[ThirdParty/AS238] Feat: parse using-namespace directives`

- [ ] **P3.2** 补齐 builder / symbol lookup 对 `using namespace` 的消费逻辑
  - 背景信息：`changes2.html` 直接记录了“builder 在 using namespace 场景下找不到 inherited class”的 2.38 修复，这说明 `using namespace` 不是单纯 parser 功能，而是会穿透类型查找、继承解析和 child funcdef 绑定。
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptengine.cpp`
  - 明确覆盖上游 changelog 中“builder 在 using namespace 场景下找不到父类/继承类”的修复点
  - 目标是让类型查找、父类解析、child funcdef 与嵌套命名空间行为和上游 `2.38` 对齐
  - 预期结果：继承链、child funcdef、局部导入离开作用域后的可见性都能在 pipeline 测试中稳定复现
  - 落实方案：
    - 优先按上游 `test_namespace.cpp` 的冲突样例补实现：同名类、同名枚举、块级 using 离开作用域、父类解析
    - 对 child funcdef / nested namespace 的 subtype 查找路径做定向审计，避免只修“普通类查找”，结果 child funcdef 仍错
  - 验证方式：
    - `Compiler.EndToEnd.NamespaceImports` 覆盖冲突与继承案例
    - 至少一条 child funcdef 或 nested namespace 用例通过
  - 依赖与注意：如 P0.4 发现预处理器/compile pipeline 会先改写 namespace 相关源码，本项必须先补前置入口再谈 builder。
- [ ] **P3.2** 📦 Git 提交：`[ThirdParty/AS238] Fix: align namespace import symbol lookup and inheritance`

- [ ] **P3.3** 建立 `using namespace` 的 end-to-end 回归
  - 背景信息：仅靠 parser/内部测试无法证明 `using namespace` 能穿过当前插件的注解编译链路；必须把它放进 end-to-end pipeline，才知道是否会和 UE 扩展语法、预处理、类生成入口冲突。
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptParserTests.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`
  - 必须覆盖：多命名空间冲突、块级导入生效范围、嵌套 namespace、继承路径
  - 落实方案：
    - parser test 专注 AST
    - pipeline test 专注真实脚本穿透 `CompileAnnotatedModuleFromMemory()` 后的行为
    - 必要时再在 `PreprocessorTests.cpp` 补一条“using namespace 原样保留”测试，防止前置重写把语句吃掉
  - 验证方式：parser 与 pipeline 两层至少各有一条正向样例和一条冲突/失败边界样例。
  - 依赖与注意：不建议把所有 namespace 行为都压在 pipeline 层，否则失败时无法判断是 parser 问题还是 compile path 问题。
- [ ] **P3.3** 📦 Git 提交：`[Test/AS238] Test: add namespace-import parser and pipeline coverage`

### Phase 4：`foreach`、bool conversion、member-init/default-copy 语义闭环

> 目标：把当前“属性已暴露”或“本地已有 lowering”但语义未完全验证的 2.37/2.38 能力，收敛为真正可依赖的语言行为。

- [ ] **P4.1** 审计并补齐 `foreach` 语义
  - 背景信息：当前本地已经有 `[UE++]` `CompileForeachStatement()` lowering，但上游 2.38 changelog 又明确列出了 `auto@`、`auto` handle 选择、script array/dictionary 支持等后续修复，说明“能编译 foreach”与“行为等价于 2.38”不是同一件事。
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`
  - 对照 `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_foreach.cpp`
  - 审计当前 `[UE++]` lowering 是否正确覆盖：
    - `auto` / `auto@`
    - key + value 双变量
    - const overload
    - 复杂 iterator 类型
    - `opForValue` / `opForKey` / `opForNext` 与容器临时变量生命周期
  - 首批行为验证落到 `AngelscriptControlFlowTests.cpp`，必要时用 `AngelscriptCompilerTests.cpp` 补 bytecode 级覆盖
  - 落实方案：
    - 先把上游 `test_foreach.cpp` 中最关键的几类场景摘成当前仓库最小回归集
    - 逐项比对当前 lowering 是否正确生成容器临时变量、iterator 临时变量和值/key 绑定顺序
    - 特别审计 `auto` / `auto@` 的 handle 推导逻辑，因为 changelog 已明确这块在 2.38 修过两轮
  - 验证方式：
    - `AngelscriptControlFlowTests.cpp` 行为级验证
    - 必要时 bytecode 或 compiler test 验证 lowering 没有产生明显错误的临时变量生命周期
  - 依赖与注意：若 stock parser 已能生成 `snForEach`，优先保留现有 parser 入口，只修 compiler lowering；不要为了追求“更像上游”而重写整个 foreach 解析路径。
- [ ] **P4.1** 📦 Git 提交：`[ThirdParty/AS238] Feat: align foreach semantics with upstream tests`

- [ ] **P4.2** 接通 contextual conversion to bool
  - 背景信息：当前 explore 结果只证明 `boolConversionMode` 已存入 engine property，但尚未证明 compiler 在条件表达式和布尔运算里真的读取了它；上游 2.38 又明确把这项能力作为 Script language 改动列出。
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`
  - 让 `asEP_BOOL_CONVERSION_MODE` 不再只是 set/get 存储，而是真正影响条件表达式与布尔运算的编译语义
  - 明确保留与当前分支不兼容脚本的失败边界，不做静默宽松化
  - 首批验证：`Internals/AngelscriptCompilerTests.cpp` 中同时覆盖 mode=0 与 mode=1，两种模式下的编译通过/失败边界都要显式断言
  - 落实方案：
    - 明确限定“contextual”范围：`if/while/for 条件`、逻辑与或、三目条件，不默认扩散到所有隐式转换
    - 先写 mode=0 失败 / mode=1 成功的成对测试，再让 compiler 根据 property 分支选择转换规则
    - 对错误消息尽量复用或对齐上游 improved error message，而不是自造模糊报错
  - 验证方式：
    - mode=0 与 mode=1 各至少一条正反样例
    - handle/object/primitive 三类输入至少覆盖两类，证明不是只对单一类型凑巧成立
  - 依赖与注意：该能力最容易引入“静默放宽”回归，所以必须保留失败边界，不能只看通过案例。
- [ ] **P4.2** 📦 Git 提交：`[ThirdParty/AS238] Feat: implement contextual bool conversion mode`

- [ ] **P4.3** 审计 member init / default copy / default copy construct 语义
  - 背景信息：`2.37` 已引入 auto generated copy constructor/default copy 规则，`2.38` 又补了 `asEP_MEMBER_INIT_MODE` 与“构造函数里显式 member init 覆盖声明初始化”的语义，再加上 changelog 中关于 inherited member 初始化时机和 explicit copy constructor 的修复，这一主题天然是 `2.37 + 2.38` 的组合闭环。
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`
  - 核对当前 `AngelscriptEngine.cpp` 初始化值与 compiler 真实行为是否一致
  - 补齐“成员初始化顺序”和“默认复制/默认复制构造自动生成策略”在当前分支中的行为差异
  - 首批验证：`Internals/AngelscriptCompilerTests.cpp` 断言成员初始化顺序与默认 special member 生成策略；`AngelscriptUpgradeCompatibilityTests.cpp` 断言 property 值与行为一致
  - 落实方案：
    - 先拆成两个子主题处理：member initialization order；default copy/copy construct generation rule
    - 对 member-init，明确“声明处初始化”和“构造函数初始化列表/等价语法”谁覆盖谁
    - 对 default copy 系列，明确 `always implement / never implement / compatibility mode` 三种行为边界
  - 验证方式：
    - 类构造行为测试 + property/engine config 测试双层覆盖
    - 至少一条 inherited member 初始化案例，验证不会在父类初始化前非法访问成员
  - 依赖与注意：该项很容易与宿主 UCLASS default/property default 语义混淆；若涉及 UE 注解类的默认值行为，应额外用 compiler pipeline 测试隔离验证。
- [ ] **P4.3** 📦 Git 提交：`[ThirdParty/AS238] Feat: close member-init and default-copy semantics`

### Phase 5：与目标主题直接相关的 runtime / restore / hot-reload 优化与修复

> 目标：只迁移那些会直接影响本次主题可用性或稳定性的 2.38 修复，不做无边界的 bugfix 大扫除。

- [ ] **P5.1** 迁移与 template / variadic / namespace 直接相关的稳定性修复
  - 背景信息：`changes2.html` 中与本计划直接相关的 bugfix 非常集中：template function 引用计数、缺少 subtype 时崩溃、variadic stack pointer / save-load、`using namespace` 继承查找、try/catch stack restore 等。这些不是锦上添花，而是把新能力从“能跑”变成“可回归”的必要条件。
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_restore.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp`
  - 重点覆盖：
    - template function 的引用计数与序列化
    - variadic 调用的栈处理
    - `using namespace` 相关的查找/恢复行为
    - 与当前启用特性直接相关的 try/catch stack restore 问题
  - 首批验证：`Internals/AngelscriptRestoreTests.cpp` 与 `HotReload/AngelscriptHotReloadFunctionTests.cpp` 至少各新增一条“重新编译后仍可调用”的正向用例
  - 落实方案：
    - 只回迁与当前主题直接相关的修复，不做“顺手把所有 2.38 bugfix 都带上”
    - 对每个 backport 的修复都标出它保护的是哪个主题（template / variadic / namespace / foreach / try-catch）
    - 优先处理会导致崩溃、无效 bytecode、restore 失败的项，其次才是错误信息或小型行为差异
  - 验证方式：
    - `AngelscriptRestoreTests.cpp` 覆盖 save/load
    - `AngelscriptHotReloadFunctionTests.cpp` / `ScenarioTests.cpp` 覆盖 discard/recompile
    - 必要时以 `test_exception.cpp` 对照 try/catch stack pointer 恢复边界
  - 依赖与注意：只有在对应主题主功能已经接通后，稳定性修复的验证才有意义，因此本项要放在 feature port 之后，不要提前孤立 backport。
- [ ] **P5.1** 📦 Git 提交：`[ThirdParty/AS238] Fix: backport targeted runtime and restore stability fixes`

- [ ] **P5.2** 记录 `computed goto` 与平台优化为后续观察项，不纳入本计划默认实施
  - 背景信息：虽然 `2.37/2.38` 对 runtime performance 与 `computed goto` 有明确改进，但 Oracle 评审已指出这更偏“优化主题”而非“迁移主题”，且对当前 Win64/MSVC 主线存在额外平台风险。
  - 修改 `Documents/Plans/Plan_AS238NonLambdaPort.md` 本文档；如确有必要，再补 `Documents/Guides/Build.md`
  - 落实方案：
    - 在本计划中明确声明：`computed goto` 不作为默认迁移交付物
    - 仅在后续独立优化计划中，基于实际平台证据再决定是否启用
    - 若实施过程中发现某项 correctness 修复必须顺带触碰 `as_context.cpp` 的 `computed goto` 分支，也应保持“只修 correctness，不做性能开关扩大”
  - 验证方式：文档中明确把该主题降级为观察项；本次实施验收标准不包含它。
  - 依赖与注意：这项降级是为了保护主线范围，不代表永久不做，而是避免其在本计划里抢占 template/namespace/foreach/variadic 的交付优先级。
- [ ] **P5.2** 📦 Git 提交：`[AS238] Docs: defer computed-goto optimization to follow-up work`

### Phase 6：测试矩阵与文档收口

> 目标：让本次迁移不只是在 ThirdParty 里“改出了代码”，而是形成清晰、分层、可重复执行的验证入口。

- [ ] **P6.1** 补齐 internals 单元测试
  - 背景信息：当前 `Internals/` 层已经有 parser / compiler / bytecode / restore 的分层入口，这是最适合承接 feature-first 红绿灯测试的地方；如果继续把这些测试拖到最后，前面几个 Phase 很容易沦为“改代码凭感觉”。
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptParserTests.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptCompilerTests.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBytecodeTests.cpp`
  - 覆盖：`using namespace` parser、template function 编译、bool conversion、member-init/default-copy、foreach lowering
  - 落实方案：
    - 不再把所有单元测试留到最后统一补，而是在每个主题第一次实现前先落至少一条失败测试
    - 这里负责把那些散落在各 Phase 中先补的失败测试收束成完整单元测试矩阵
  - 验证方式：`Internals.Parser.*`、`Internals.Compiler.*`、`Internals.Bytecode.*` 至少能分别跑出与本计划主题对应的用例。
  - 依赖与注意：如果某主题在前序 Phase 已经落了红绿测试，这里做的是“补齐矩阵”，不是重复写一份近似测试。
- [ ] **P6.1** 📦 Git 提交：`[Test/AS238] Test: add parser-compiler-bytecode unit coverage`

- [ ] **P6.2** 补齐 language / pipeline 行为测试
  - 背景信息：`AngelscriptFunctionTests.cpp` 和 `Compiler.EndToEnd.*` 代表的是“用户可感知行为层”，也是最容易暴露旧负例边界过时的地方；这层测试必须在功能落地后尽快反映新边界。
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`
  - 将当前 `Functions.Pointer` / `Functions.Template` 负例，按新能力拆分为“新正例 + 仍应失败的旧边界”
  - 落实方案：
    - `FunctionTests` 负责 function/template/variadic 可调用行为
    - `ControlFlowTests` 负责 foreach
    - `CompilerPipelineTests` 负责 compile path 与 UE 注解语法共存
    - 每个旧负例翻转时都要留下“仍失败的边界”证明这不是无约束放开
  - 验证方式：
    - `Angelscript.TestModule.Angelscript.Functions.*`
    - `Angelscript.TestModule.Angelscript.ControlFlow.*`
    - `Angelscript.TestModule.Compiler.EndToEnd.*`
  - 依赖与注意：如果某个主题只在 internals 测试通过、行为测试仍失败，则该主题不能算验收完成。
- [ ] **P6.2** 📦 Git 提交：`[Test/AS238] Test: add language and pipeline feature coverage`

- [ ] **P6.3** 补齐 restore / hot-reload 回归
  - 背景信息：本仓库不是纯解释执行基线，`restore` / `module discard` / `hot reload` 是真实使用路径；template/variadic/namespace 这类新能力如果只在首编通过，后续模块生命周期阶段出错，等同于没交付。
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`
  - 覆盖 template function、namespace import、variadic 调用、foreach 与模块重新编译/恢复的稳定性
  - 落实方案：
    - 至少为 template/variadic 各留一条“保存/恢复后仍可用”或“重编后仍可用”的回归
    - 若当前 helper 不足以直接做完整 save/load，则优先用现有 restore/hot-reload 等价入口建立最近似验证，而不是把本项删掉
  - 验证方式：`Internals.Restore.*` 与 `HotReload.*` 至少各跑一组本主题用例；失败时要能明确归因到 restore 还是 hot reload。
  - 依赖与注意：本项只在 P5.1 之后执行，否则很可能被已知稳定性 bug 拦住，导致测试结论失真。
- [ ] **P6.3** 📦 Git 提交：`[Test/AS238] Test: add restore and hot-reload regression coverage`

- [ ] **P6.4** 同步文档与测试运行入口
  - 背景信息：一份好的迁移计划不只要告诉实现者“改哪里”，还要告诉后来的维护者“怎么验证、从哪里继续接手”；当前仓库已经有 `Documents/Guides/Test.md`，最适合作为正式入口汇总点。
  - 修改 `Documents/Guides/Test.md`
  - 视需要新增 `Documents/Knowledges/` 下的 `AS238` 主题知识文档
  - 明确推荐运行入口：
    - `Angelscript.TestModule.Internals.Parser.*`
    - `Angelscript.TestModule.Internals.Compiler.*`
    - `Angelscript.TestModule.Angelscript.Functions.*`
    - `Angelscript.TestModule.Compiler.EndToEnd.*`
    - `Angelscript.TestModule.HotReload.*`
  - 建议最小命令集：
    - `Automation RunTests Angelscript.TestModule.Internals.Parser; Quit`
    - `Automation RunTests Angelscript.TestModule.Internals.Compiler; Quit`
    - `Automation RunTests Angelscript.TestModule.Angelscript.Functions; Quit`
    - `Automation RunTests Angelscript.TestModule.Compiler.EndToEnd; Quit`
    - `Automation RunTests Angelscript.TestModule.HotReload; Quit`
  - 落实方案：
    - 在 `Test.md` 中新增一个 `AS238 Non-Lambda` 小节，按 `Preprocessor / Parser / Compiler / ControlFlow / Functions / EndToEnd / Restore / HotReload` 分组列出入口
    - 如新增 `Documents/Knowledges/AngelscriptAS238NonLambda.md`，则把“主题简介、关键文件、关键测试、明确排除项”一并固化下来
  - 验证方式：文档中所有推荐命令都能映射到现有测试命名空间；后续实施者无需再次检索仓库即可找到主要入口。
  - 依赖与注意：不要只记录最终“全量测试”命令，也要保留最小分组命令，方便后续按主题回归。
- [ ] **P6.4** 📦 Git 提交：`[Docs/AS238] Feat: document non-lambda feature test entrypoints`

## 验收标准

1. `template functions` 的公共 API、注册、实例化、调用路径在当前分支中形成闭环，且不依赖 lambda 路线。
2. `variadic arguments` 至少在当前插件需要的 generic calling convention 路径上可注册、可编译、可执行。
3. `using namespace` 在 parser、builder、symbol lookup 与 end-to-end pipeline 中行为可验证。
4. `foreach` 不再只是“存在 lowering 代码”，而是对上游 `test_foreach.cpp` 的核心语义有明确回归覆盖。
5. `asEP_BOOL_CONVERSION_MODE`、`asEP_MEMBER_INIT_MODE`、`asEP_ALWAYS_IMPL_DEFAULT_COPY`、`asEP_ALWAYS_IMPL_DEFAULT_COPY_CONSTRUCT` 不再只是 set/get 表面属性，而有明确 compiler 行为与测试证据。
6. template / variadic / namespace / foreach 相关的 restore / hot-reload 回归通过。
7. `Documents/Guides/Test.md` 能指出本次迁移的主要测试入口，后续接手者无需重新摸索测试路径。
8. 整个计划实施过程中，不需要整体替换 ThirdParty，也不需要提前宣告“完整 2.38 升级完成”。

## 风险与注意事项

### 风险 1：当前基线是“2.33 版本号 + 2.38 表面 + UE++ 私有改造”的混合体

这意味着任何看似“简单的 upstream cherry-pick”都可能误伤当前 APV2/UE++ 的行为。特别是 `foreach`、object flags、module lookup、restore 这几块已经有明显本地改造痕迹。

**缓解**：每个主题都先做本地/上游矩阵，再做人工适配；不接受无审计整段替换。

### 风险 2：模板函数与 variadic 会跨越 parser / compiler / runtime / restore 多层

这不是只改一个解析函数就能结束的能力；如果只补 parser 而不补 save-load / hot-reload，后续很容易出现“首编通过、重编/恢复崩掉”的半完成状态。

**缓解**：Phase 5 / Phase 6 明确把 restore / hot-reload 回归列为必做，而不是可选加分项。

### 风险 3：当前已设置的 engine property 可能让人误判“语义已经存在”

`AngelscriptEngine.cpp` 里已经设置了多个 `2.37/2.38` property 值，但这并不自动等于 compiler 已完整尊重这些属性。

**缓解**：Phase 4 强制把 property 存储与 compiler 真实行为绑在一起审计，不能只看枚举和 set/get。

### 风险 4：`foreach` 当前已有本地 lowering，实现细节未必与上游完全一致

如果实施时直接按“我们已经有 foreach”跳过审计，后续会留下 `auto@`、key/value、多层容器临时变量生命周期等隐性差异。

**缓解**：必须以 `Reference/angelscript-v2.38.0/sdk/tests/test_feature/source/test_foreach.cpp` 为行为基线，而不是只看当前是否能编译通过简单脚本。

### 风险 5：本计划容易和 lambda 迁移计划边界重叠

上游 `2.38` 的某些 bugfix 同时触及 function pointer / template / lambda 周边逻辑。如果边界不清，实施中很容易再次把 lambda 路线混进来。

**缓解**：所有涉及 anonymous function / closure / `function(...) {}` 的改动，统一回到 `Documents/Plans/Plan_AS238LambdaPort.md`；本计划只允许处理其外围 prerequisite，不允许直接接 lambda 语义。
