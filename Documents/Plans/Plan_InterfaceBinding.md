# Angelscript 接口绑定完善计划

## 背景与目标

### 背景

当前 `Plugins/Angelscript` 已实现了一条完整的 **脚本定义 UINTERFACE** 管线（详见外部参考《AngelPortV2-UInterface-Support-Analysis.md》）：

- **预处理器**：识别 `interface` chunk → 提取方法声明到 `InterfaceMethodDeclarations` → 擦除接口块（保留行号空格） → `RegisterObjectType`（引用类型）+ `RegisterObjectMethod`（`CallInterfaceMethod` 回调）
- **类生成器**：创建接口 `UClass`（`CLASS_Interface | CLASS_Abstract`）+ minimal UFunction 存根 → `ResolveInterfaceClass` + `AddInterfaceRecursive` → `FindFunctionByName` 方法完整性校验
- **运行时**：`Cast<I>` 走 `ImplementsInterface` 分支 → `CallInterfaceMethod` 通用 generic 回调 → `FindFunction` → `ProcessEvent`
- **测试**：`Interface/` 目录下 15 个接口测试（Declare 2 / Implement 3 / Cast 3 / Advanced 7），测试路径 `Angelscript.TestModule.Interface.*`

这是一套 **插件侧模拟方案**——不走 AngelScript 引擎自带的 `interface` 编译路径，接口块在编译前被预处理器擦除，接口在 AS 编译器眼中是普通引用类型。

### 已知限制（来自外部参考《AngelPortV2-UInterface-Support-Analysis.md》 §6）

| 限制 | 说明 | 影响 |
|------|------|------|
| **UFunction 存根仅含名字** | Full Reload 生成的接口 UFunction 没有完整 `FProperty` 参数链 | 方法校验只靠 `FindFunctionByName` 同名匹配，不检查签名兼容性 |
| **FInterfaceProperty 未参与** | 运行时绑定层无 `FInterfaceProperty` 处理，编辑器侧相关代码已注释 | 无法在 AS 脚本属性中声明 `TScriptInterface<I>` 类型 |
| **TScriptInterface 未暴露** | 仅出现在自动生成的引擎 API caller 中 | 脚本侧不能用 `TScriptInterface<I>` 做属性存储和传递 |
| **接口块不进入 AS 编译** | 由预处理器擦除 + 插件侧模拟，非 AS 引擎原生 `interface` | 不能使用 AS 原生接口语法特性（如虚函数表） |
| **方法签名不做类型匹配** | 校验只检查方法名存在，不校验参数类型/个数 | 理论上方法名相同但签名不同也会通过编译 |

### C++ UInterface 支持缺口（来自同文档 §6.2，**核心问题**）

AS 类 **当前只能实现 AS 脚本中声明的接口**，**不能** 实现 C++ 侧通过 `UINTERFACE()` 宏声明的 UInterface。尽管 `ResolveInterfaceClass` 有三级回退查找能在反射层部分挂接，但 AS 脚本层完全不可用：

| # | 缺口 | 说明 |
|---|------|------|
| 1 | **未注册为 AS 类型** | C++ UInterface 没有通过 `RegisterObjectType` 注册到 AS 引擎，脚本中 `Cast<ICppInterface>()` 无法编译 |
| 2 | **方法未注册** | C++ 接口的 `UFUNCTION` 方法没有对应的 `CallInterfaceMethod` 注册，无法通过接口引用调方法 |
| 3 | **StaticClass() 不可用** | C++ 接口的 `StaticClass()` 全局变量未在 AS 中生成，脚本无法引用 `UMyInterface::StaticClass()` |

### Patch 方案参考（来自外部参考《AngelPortV2-vs-Patch-Interface-Comparison.md》）

`dev-as-interface` 分支的 Patch（外部参考《参考-dev-as-interface-interface-improvements.patch》）提供了一套完整的替代方案，核心差异：

| 维度 | AngelPortV2（当前） | Patch 方案 |
|------|-------------------|-----------|
| AS 接口模型 | 插件侧模拟：`RegisterObjectType` 引用类型 | AS 引擎原生：`RegisterInterface` 真正的 AS interface 类型 |
| 接口块处理 | 预处理器**擦除**，不进入 AS 编译器 | **保留**给 AS 编译器原生处理 |
| C++ UInterface | 不支持 | 全面支持，自动注册为 AS interface 类型 |
| FInterfaceProperty | 未参与 | 完整支持，包括属性创建/匹配/读写/getter/setter |
| ThirdParty 修改 | 无（零侵入） | 需修改 3 个 AS 引擎文件 |

Patch 方案的关键可借鉴技术点：
- **双重注册模式**：`UMyInterface`（UObject handle 兼容）+ `IMyInterface`（AS interface 新增）
- **FScriptInterface 桥接**：AS 侧始终使用 `UObject*`，在 UE 反射边界自动转换
- **接口方法继承链接**：`InheritNativeInterfaceMembers` 递归处理接口继承链
- **TypeFinder 扩展**：`FInterfaceProperty` → `FAngelscriptTypeUsage::FromClass(InterfaceClass)`

### 横向参考

- **UnrealCSharp**（`Reference/UnrealCSharp`）：双形态映射（`partial class U…` + `interface I…`），`FDynamicInterfaceGenerator` 动态创建接口 `UClass`，`FInterfacePropertyDescriptor` 完整属性支持，`UClass::Interfaces.Emplace(...)` 维护实现关系
- **PythonScriptPlugin**（`UERelease/Engine/Plugins/Experimental/`）：`GetExportedInterfacesForClass` 递归收集接口 `UClass`，接口方法"混入"实现类 Python API，`FInterfaceProperty` 通过 `GetInterfaceAddress` 构造 `FScriptInterface`

### 目标

1. **决定架构方向**：确定是否修改 ThirdParty、命名约定、FInterfaceProperty 优先级
2. **实现 C++ UInterface 脚本绑定**：补齐三个缺口，脚本能 Cast、调方法、作属性引用 C++ 接口
3. **实现 FInterfaceProperty 支持**：`TScriptInterface<T>` 属性能在脚本中读写
4. **增强方法签名校验**：编译期检测签名不匹配
5. **补齐测试与文档**：验证所有新增能力，明确支持边界

## 推荐收敛路线

这份计划不再把选择留到实现当天临场决定，而是先给出一条默认收敛路线，后续仅在 Phase 0 审计证明不成立时再改写：

1. **先冻结边界，再决定是否修改 ThirdParty**。当前仓库已经 vendored `ThirdParty/angelscript/source/`，`AngelscriptRuntime.Build.cs` 也已公开 include path；这里的难点不是“能否接入第三方库”，而是“是否值得为了接口语义去改 AngelScript 编译器/类型系统”。
2. **优先把 C++ UInterface 与 FInterfaceProperty 的插件层差距说清楚，再决定是否走编译器级原生 interface 路线**。如果目标只是让脚本识别并调用 C++ `UINTERFACE`，理论上可以先走零 ThirdParty 的插件层方案；如果目标还包括脚本声明 `interface` 享受编译器级契约与继承语义，则必须认真评估 B/C 路线。
3. **默认推荐的取舍顺序是 A → B → C，而不是直接整包采纳 Patch**。也就是先证明零侵入方案缺什么，再判断是否需要最小 `as_objecttype.cpp` 变更，最后才决定是否引入 `as_builder.cpp` 的两处联动修改。
4. **A/B/C 切换必须有明确触发条件**。只有当“零 ThirdParty 路线无法满足目标能力”被测试或代码审计证明时，才允许推进到 B；只有当“需要脚本声明 `interface` 进入 AS 编译器并由 builder/compiler 原生处理”被明确接受时，才允许从 B 推进到 C。
5. **`TScriptInterface` 的脚本侧目标形态必须与运行时事实对齐**。UE 反射边界最终存的是 `FScriptInterface`，但脚本侧更稳的载体仍然是 `UObject*`/handle；因此计划中的属性支持必须明确“脚本侧看到什么类型、边界层如何桥接、Blueprint-only / native implementation 各自怎么处理”。
6. **所有 ThirdParty 改动必须可追踪、可回退、可复核**。如果 Phase 0 结论是要修改 `ThirdParty/angelscript/source/`，则同一阶段必须同步约定 `[UE++]` / `[UE--]` 标记、变更清单归档路径，以及与 `P9-A-Upgrade-Roadmap.md` 的冲突复核方式。

## 实施纪律

- 所有涉及接口能力边界翻转的条目，都先补失败测试或 compile-pipeline 审计，再落代码；不能先改 `ThirdParty` 再补测试。
- 所有涉及 `ThirdParty/angelscript/source/` 的条目，都必须写清楚“为什么插件层做不到、为什么这 1~3 个 hunk 是最小必要改动”。
- 所有新条目默认写 repo 内相对路径；外部专题文档只写文档名，不再写本地绝对盘符路径。
- 若仓库中尚无 `Documents/Knowledges/AngelscriptChange.md`，则 Phase 0 需要把它一并补出来，专门记录接口相关 ThirdParty 变更点、上游来源与回退策略。

## 当前事实状态

```text
接口相关代码分布（AngelscriptRuntime/）：

Preprocessor/AngelscriptPreprocessor.h    ← EChunkType::Interface
Preprocessor/AngelscriptPreprocessor.cpp  ← interface 块解析、方法提取、块擦除、
                                             RegisterObjectType + RegisterObjectMethod(CallInterfaceMethod)
Core/AngelscriptEngine.h                  ← FInterfaceMethodSignature（仅 FName FunctionName）、
                                             FAngelscriptClassDesc 中 bIsInterface / ImplementedInterfaces /
                                             InterfaceMethodDeclarations
Core/AngelscriptEngine.cpp                ← RegisterInterfaceMethodSignature / ReleaseInterfaceMethodSignature
ClassGenerator/AngelscriptClassGenerator.cpp ← CallInterfaceMethod 实现（55-98行）、
                                               接口 UClass 创建（2788-2861行）、
                                               FinalizeClass 接口挂接（5081-5209行）
Binds/Bind_UObject.cpp                    ← opCast 接口分支（134-169行）、ImplementsInterface 绑定
Binds/Bind_BlueprintType.cpp              ← 无接口相关改动（Patch 核心改动点 +500行）
Binds/Bind_Helpers.h                      ← 无接口相关 helper（Patch 新增 +80行）

测试（AngelscriptTest/Interface/）：
  AngelscriptInterfaceDeclareTests.cpp      ← 2 个用例
  AngelscriptInterfaceImplementTests.cpp    ← 3 个用例
  AngelscriptInterfaceCastTests.cpp         ← 3 个用例
  AngelscriptInterfaceAdvancedTests.cpp     ← 7 个用例（含 CppInterface 用例，
                                               但实际是脚本声明的接口，非 C++ 声明绑定）
AngelscriptTest/Angelscript/
  AngelscriptInheritanceTests.cpp           ← 语言级 interface 负例（期望编译失败）
```

能力边界：
- 脚本定义 `UINTERFACE` + 脚本类实现 → **完整闭环，15 个测试**
- C++ 定义 `UInterface` + 脚本类实现 → **反射层部分可用，AS 脚本层不可用**
- `TScriptInterface<T>` 属性 → **未支持**
- AS 语言级 `interface I…` → **明确不支持**
- 接口方法签名完整性 → **仅函数名匹配，不校验参数**

## 测试对象与生产代码映射

为避免执行时“知道要做哪个 Phase，但不知道它真正保护哪段实现”，先固定主要测试对象与生产代码映射。后续若新增任务，也应优先落到下表对应的生产职责上，而不是把多个独立问题揉进一个改动里。

| 能力层 | 主要生产文件 | 主要测试文件 | 重点函数 / 结构 | 当前缺口 |
|---|---|---|---|---|
| 脚本接口预处理 | `Preprocessor/AngelscriptPreprocessor.h/.cpp` | `AngelscriptTest/Interface/AngelscriptInterfaceDeclareTests.cpp`、`AngelscriptInheritanceTests.cpp` | `EChunkType::Interface`、interface block 检测/擦除/提取 | 只支持脚本声明接口，不保留原生 interface 语义 |
| 接口类型描述 | `Core/AngelscriptEngine.h/.cpp` | `AngelscriptTest/Interface/*` | `FInterfaceMethodSignature`、`ImplementedInterfaces`、`InterfaceMethodDeclarations` | C++ UInterface 未注册为脚本可见类型 |
| 类生成与接口挂接 | `ClassGenerator/AngelscriptClassGenerator.cpp` | `AngelscriptInterfaceImplementTests.cpp`、`AngelscriptInterfaceAdvancedTests.cpp` | `CallInterfaceMethod`、接口 UClass 创建、`FinalizeClass` | 只校验方法名，不校验签名 |
| UObject Cast / 反射桥接 | `Binds/Bind_UObject.cpp` | `AngelscriptInterfaceCastTests.cpp` | `opCast` 接口分支、`ImplementsInterface` | 只对已模拟注册的脚本接口闭环稳定 |
| Blueprint / Property 绑定 | `Binds/Bind_BlueprintType.cpp`、`Binds/Bind_Helpers.h` | 新增 `InterfaceProperty` 场景测试 | `CreateProperty`、`MatchesProperty`、参数/返回值桥接 | 缺少 `FInterfaceProperty` 与 `FScriptInterface` 支持 |
| C++ UInterface 注册与方法暴露 | `Binds/Bind_BlueprintType.cpp`、`Core/AngelscriptEngine.cpp`、`ClassGenerator/AngelscriptClassGenerator.cpp` | 新增 `CppInterface` 场景测试 | 类型注册、方法扫描、继承链接、`StaticClass()` 暴露 | 脚本侧不可见/不可调 |
| 签名校验 | `ClassGenerator/AngelscriptClassGenerator.cpp`、接口 UFunction 存根生成路径 | 新增 `Signature` 测试 | `FinalizeClass`、接口方法 UFunction 构造 | 目前只做函数名匹配 |

执行时若发现某个测试文件开始同时覆盖“接口类型注册 + FInterfaceProperty + 预处理器改造”这类多个独立职责，应优先拆文件与拆任务，而不是继续扩大单个条目范围。

## 实施细化约定

为避免后续再次把执行项写成“只有标题，没有做法”，本计划中所有 `- [ ]` 任务默认都按以下规则理解；如果条目本身没写到某个方面，也应遵循本节。

### 通用任务拆解模板

每个执行条目都应至少回答以下四件事：

1. 这一步要改哪些文件，这些文件各自承担什么角色。
2. 这一步打算怎么实现，最小闭环是什么，不能只写“支持某功能”。
3. 这一步完成后跑什么验证，预期看到什么结果。
4. 什么结果算完成，什么情况仍然属于“半成品”。

### 通用文件职责边界

- `Preprocessor/*`
  - 只负责接口 chunk 的检测、提取、保留/擦除策略，不顺手承担类型注册或运行时桥接。
- `Core/AngelscriptEngine.*`
  - 只负责接口签名数据、注册表、全局桥接信息，不直接写具体 UObject 绑定逻辑。
- `ClassGenerator/*`
  - 只负责 UClass/UFunction 生成、接口挂接和最终校验，不承载 property getter/setter 细节。
- `Binds/*`
  - 只负责把 UE 反射类型和运行时调用桥接到脚本，不承担预处理器语义判断。
- `AngelscriptTest/Interface/*`
  - 每个测试文件只聚焦一个主题：C++ 接口注册、FInterfaceProperty、签名校验、预处理器保留等，不把全部接口回归堆在一个大文件里。

### 通用测试结构

对于本计划中新增或扩展的接口测试，统一使用以下结构组织单个用例：

1. **Setup**
   - 生成或加载当前场景需要的脚本/原生接口 fixture
   - 编译模块或创建原生类/接口测试对象
2. **Action**
   - 执行接口声明、实现、Cast、属性读写、方法调用或编译期校验流程
3. **Assert**
   - 明确断言编译是否成功、错误文本是否匹配、`ImplementsInterface` 是否成立、返回值是否正确、属性桥接是否成立
4. **Cleanup**
   - 丢弃模块、恢复共享测试引擎或清理原生测试状态，避免污染后续用例

### 通用验证约定

- 只要任务涉及脚本可见能力，就至少补一条正例和一条负例；不能只写 happy path。
- 只要任务涉及编译诊断，就至少说明“期望报错的关键文本”或“最小可观察失败信号”。
- 只要任务涉及 `FScriptInterface` / `FInterfaceProperty`，就同时说明对象指针、接口指针以及无效对象的处理方式。
- 只要任务涉及 ThirdParty 修改，就必须写明 `//[UE++]` / `//[UE--]` 标记策略以及升级冲突记录位置。

### 通用完成判据

- 不允许以“代码编译通过”作为唯一完成标准。
- 若某条任务宣称“支持某能力”，必须能指出至少一个新增测试或回归测试来证明。
- 若某条任务本质上只是调研/决策，也必须留下明确决策产物，而不是只在对话里口头说明。

## 架构决策点

在开始分阶段执行之前，需确认以下三个决策点。Phase 0 的任务就是完成这些决策。

### 决策 1：是否修改 ThirdParty

| 选项 | 代价 | 收益 |
|------|------|------|
| **A：不修改**（当前策略） | 无升级风险 | 只能用 `RegisterObjectType` + `CallInterfaceMethod` 模拟，无 AS 编译器级类型检查 |
| **B：修改 `as_objecttype.cpp`** | 1 处修改（启用 `IsInterface()`），升级时需合入 | 获得 AS 原生 interface 语义，`RegisterInterface` 可用 |
| **C：全量采纳 Patch 的 3 处修改** | `as_objecttype.cpp` + `as_builder.cpp` x2 | 最完整：AS 编译器自动处理接口继承和方法契约 |

### 决策 2：命名约定

| 选项 | 示例 | 兼容性 |
|------|------|--------|
| **A：仅 U 前缀**（当前策略） | `UTestDamageableInterface` | 向后兼容，但 AS 编译器不识别接口语义 |
| **B：双重注册 U+I**（Patch 方案） | `UTestDamageableInterface`（handle）+ `ITestDamageableInterface`（interface） | 兼容现有代码 + 新增接口语义 |
| **C：仅 I 前缀** | `ITestDamageableInterface` | 与 UE C++ 习惯一致，但破坏向后兼容 |

### 决策 3：FInterfaceProperty 优先级

| 选项 | 说明 |
|------|------|
| **先跳过** | Phase 先只做 C++ 接口的 Cast + 方法调用，FInterfaceProperty 后续单独做 |
| **一起做** | 一次性完成全部 FInterfaceProperty 支持（Patch 改动约 200 行在 `Bind_BlueprintType.cpp`） |

## 已确定决策（Phase 0 产物）

> 本节在 Phase 0 完成后回填，后续 Phase 只能引用这里已经冻结的结论，不能继续隐式改方向。

- **决策 1：ThirdParty 修改** — `待定`；需给出最终选项（A/B/C）、触发理由、涉及文件、升级冲突面、回退方式。
- **决策 2：命名约定** — `待定`；需给出最终对外写法（`U` / `I` / 双重注册）、兼容策略、脚本示例。
- **决策 3：FInterfaceProperty 优先级** — `待定`；需给出是 Phase 4 独立落地还是与 Phase 1~3 同期推进，以及延期时的临时使用边界。
- **执行分支：`AS_USE_BIND_DB` 路径归属** — `待定`；需明确 Phase 2/4 首批实现覆盖 `AS_USE_BIND_DB`、`!AS_USE_BIND_DB` 还是两者同时覆盖，并写入验证矩阵。
- **接口无效赋值策略** — `待定`；需冻结“拒绝赋值并报诊断”或“清空为 null 并报诊断”等单一行为，后续属性 setter、getter/setter helper、参数/返回值桥接与测试统一复用。

## 分阶段执行计划

### Phase 0：架构决策与方案定型

> 目标：完成三个架构决策点，确定后续 Phase 的技术路线。

- [ ] **P0.1** 评估 ThirdParty 修改的可行性
  - 对照 Patch 的 3 处 AS 引擎修改（`as_objecttype.cpp` `IsInterface()` 启用、`as_builder.cpp` `CreateDefaultDestructors` null 检查、`as_builder.cpp` `DetermineTypeRelations` 接口继承）
  - 评估对当前 AS 2.33 和计划中 AS 2.38 升级的影响
  - 确认修改是否与 `P5-C-AS238-Breaking-Changes.md` / `P9-A-Upgrade-Roadmap.md` 冲突
  - 参考 `AngelPortV2-vs-Patch-Interface-Comparison.md` §4 优劣势对比
  - 记录决策结果到本 Plan 的"已确定决策"章节（新增）
  - 当前仓库的真实前提是：`AngelscriptRuntime.Build.cs` 已经暴露 `ThirdParty/angelscript/source` 和根目录 include path，因此这里评估的不是“是否能接第三方依赖”，而是“是否要修改 vendored AngelScript 源码来换取原生 interface 语义”。这一点必须在结论里写明，避免后续实施者把问题误读成 Build.cs 或打包问题。
  - 落实时先做一张三列对照：`方案 A/B/C`、`需要修改的文件`、`能补齐的能力边界`。至少要把 `C++ UInterface 可见性`、`脚本 interface 是否进入 AS 编译器`、`方法签名校验能否前移到编译器`、`升级到 2.38 时的合并成本` 四项逐一写清楚。
  - 若结论倾向 B/C，必须同步约定 ThirdParty 变更追踪方式：相关 hunk 使用 `[UE++]` / `[UE--]` 标记，并把文件列表与来源补进 `Documents/Knowledges/AngelscriptChange.md`（若文件不存在，本项顺手创建）。不能等实现阶段再临时决定标记规则。
  - 本项要输出明确的 go/no-go 门槛：什么证据说明 A 已经足够、什么能力缺口迫使 A→B、什么新增目标又会迫使 B→C。推荐至少把三条门槛写成决策规则：`仅需 native C++ UInterface 可见/可调 → 优先 A`；`需要 AS 原生 interface type 语义但暂不改脚本编译链 → 才考虑 B`；`需要脚本声明 interface 进入 compiler/builder 并建立原生契约/继承关系 → 才进入 C`。
  - 同时把 `AS_USE_BIND_DB` 的实施归属作为本项输出，而不是 Phase 2 临时决定。至少要明确：首批实现是只覆盖当前主构建路径，还是两条路径一起落；若先只做一条，另一条如何在计划中标记 `N/A` 或后续补齐。
  - 验证结果不是一句“建议选 X”，而是一份可执行决策记录：包含最终选项、触发原因、为何不选另外两项、涉及源文件、与 `P9-A-Upgrade-Roadmap.md` 的冲突等级、以及一条明确的回退方式。
- [ ] **P0.1** 📦 Git 提交：`[Interface] Docs: record ThirdParty modification decision`

- [ ] **P0.2** 确定命名约定和 FInterfaceProperty 优先级
  - 基于决策 1 结果选择命名约定（若选 B 用双重注册则需 Patch 式 `GetNativeInterfaceScriptName` U→I 转换）
  - 确定 FInterfaceProperty 是否与 C++ UInterface 同期实现
  - 更新本 Plan，将不适用的 Phase 标记为 N/A 或调整内容
  - 这一项不能只停在“拍板名字”。它要同时冻结三件事：脚本侧公开给用户看的类型名、运行时 `UserData/UClass*` 怎么映射、以及老脚本使用 `U...Interface` 时是否还能继续编译。若命名约定不冻结，后续 `BindUClass`、TypeFinder、文档示例都会来回返工。
  - 命名判断时要同时参考 UE 自身 `UInterface + IInterface` 的双类模式、Hazelight 的自动绑定风格、以及当前仓库已经存在的 `FAngelscriptType::GetBoundClassName` 结果。若选择双重注册，必须明确谁是“handle 兼容名”、谁是“原生 interface 名”、脚本示例怎么写、`StaticClass()` 暴露在 U 名还是 I 名上。
  - 本项必须额外冻结命名规则是否**仅适用于 native C++ UInterface**，还是也扩展到脚本声明接口。若脚本接口仍保持当前 `U...` 风格而 native 接口走 `U/I` 双重注册，需要在本节明确写出，避免知识文档把两类接口混成同一公开模型。
  - FInterfaceProperty 优先级的决定必须写出临时边界。若决定延后，就要在本节明确：Phase 1~3 期间脚本侧只能继续用 `UObject*`/handle + `ImplementsInterface()` 方式流转接口对象，不允许在文档里暗示“属性也快有了”。若决定一起做，就要同步确认 `Bind_BlueprintType.cpp`、TypeFinder、getter/setter helper 与测试文件会在同一批次进入范围。
  - 本项还要冻结“无效接口赋值”的单一行为，并写进 `已确定决策`。后续 `FInterfaceProperty` setter、参数桥接、返回值桥接、Blueprint getter/setter helper 与测试都必须复用这同一个策略，不能各自定义失败语义。
  - 验证产物至少包括一张小表：`语义目标 / 选定写法 / 兼容性说明 / 对应 Phase`。更新完本 Plan 后，所有不再适用的 alt 路线都要明确标注 `N/A` 或降级条件，不能保留成模糊候选项。
- [ ] **P0.2** 📦 Git 提交：`[Interface] Docs: finalize naming convention and FInterfaceProperty priority`

### Phase 1：C++ UInterface 脚本绑定 — 类型注册

> 目标：让 C++ 定义的 UInterface 在 AS 脚本中可见，解决缺口 1（未注册为 AS 类型）和缺口 3（StaticClass 不可用）。

**方案 A（零 ThirdParty，`RegisterObjectType` 模拟）：**

- [ ] **P1.1** 在 `Bind_BlueprintType.cpp` 的 `BindUClass` 流程中，对 `IsNativeInterfaceClass(Class)` 的 UClass 额外注册 AS 类型
  - 参考 Patch 的 `IsNativeInterfaceClass` 判定（`CLASS_Interface` + 非 `UInterface::StaticClass()`）
  - 使用 `RegisterObjectType` 注册（当前模拟模式）或 `RegisterInterface`（若决策 1 选 B/C）
  - 确保 `UserData` 设置为对应 `UClass*`（与 `ApplyUnrealClassMetadataToScriptType` 模式一致）
  - 当前真实代码路径是：`ShouldBindEngineType()` 枚举 native `UClass`，随后 `BindUClass(Class, ClassName)` 注册类型，最后才 `BindUClassLookup()` 建立属性反查。也就是说，这一步不是凭空新增一个接口入口，而是要在现有 native type bind 主干里插入“接口类的额外注册/元数据补齐”。
  - 若决策 1 仍选 A，本项的重点是让 C++ `UINTERFACE` 至少在脚本类型系统中可见，并把 `ScriptType->GetUserData()` 稳定映射回 `UClass*`；这样 `FinalizeClass` 的 `ResolveInterfaceClass()` 才有机会通过类型系统路径解析到 native 接口，而不只是走最终的 `TObjectIterator<UClass>` 兜底。
  - 若决策 1 选 B/C，本项就不只是“注册一个名字”，还要同步把 editor-only / tooltip / disallow-instantiation 这类 Unreal 元数据传进 script type。否则接口类型即便注册成功，也会和其它 BlueprintType/UClass 的文档与编辑器表现不一致。
  - 落地时必须写清楚两个输出：一是脚本里能否直接声明该接口类型变量，二是 `GetTypeInfoByName()` / `GetUserData()` 能否回到同一个 `UClass*`。没有这两个观测点，就无法证明“注册为 AS 类型”真的闭环。
- [ ] **P1.1** 📦 Git 提交：`[Interface] Feat: register C++ UInterface as AS type in BindUClass`

**方案 B（修改 ThirdParty，`RegisterInterface` 原生）：**

- [ ] **P1.1-alt** 先应用 `as_objecttype.cpp` 的 `IsInterface()` 启用修改
  - 修改内容：将注释掉的 `if ((flags & asOBJ_SCRIPT_OBJECT) && size == 0) return true;` 取消注释
  - 使用 `//[UE++]` / `//[UE--]` 标记
  - 然后在 `BindUClass` 中使用 `RegisterInterface` 而非 `RegisterObjectType`
  - 这条 alt 路线只有在 P0.1 已明确“需要原生 interface 语义”时才执行；否则它只是无谓扩大 ThirdParty 改动面。文档里要明确写出：采用本项的直接收益是什么，单改 `BindUClass` 仍然缺什么，以及为什么不能继续停留在 `RegisterObjectType` 模拟层。
  - 这里的实施粒度必须非常小：只动 `as_objecttype.cpp` 中 `IsInterface()` 相关 hunk，并在同一提交说明它对应哪份外部 patch/上游语义。不要在这个提交里顺手混入 `as_builder.cpp`、helper 或绑定层改动，否则后续很难定位回归来源。
  - 若执行本项，验证必须包含一条“AS 原生 interface 类型确实被识别”的证据，例如 `RegisterInterface` 成功、`TypeInfo->flags` 具备预期 interface 语义、或后续 `DetermineTypeRelations` 的分支开始可达。否则只改了 `IsInterface()` 并不能说明路线已经成立。
- [ ] **P1.1-alt** 📦 Git 提交：`[ThirdParty] Feat: enable AS native interface semantics for IsInterface()`

- [ ] **P1.2** 确保 C++ 接口的 `StaticClass()` 全局变量在 AS 中可用
  - 确认 `BindStaticClass` 是否已为接口类型生成（当前 `BindUClass` 之后会调 `BindStaticClass`）
  - 若未覆盖，参考 Patch 的 `BindStaticClass` 调用逻辑补齐
  - 验证脚本中 `UMyInterface::StaticClass()` 可编译
  - 当前 `Bind_BlueprintType.cpp` 的常规路径会在类型/属性绑定后统一调用 `BindStaticClass(Binds, DBBind.TypeName, Class)`；本项要核对的是接口类是否被某个前置分支过滤掉，而不是另起一套静态类绑定系统。
  - 若 Phase 0 选了双重注册，本项还要顺手冻结“谁暴露 `StaticClass()`”的规则：通常应继续由 `U...Interface` 这个 Unreal 反射名承担 `StaticClass()`，而 `I...Interface` 负责脚本 interface 语义。否则脚本示例会混乱，也会破坏现有 `ImplementsInterface(UMyInterface::StaticClass())` 的直觉。
  - 验证不能只看编译通过，还要在运行时断言返回的 `UClass*` 非空且具备 `CLASS_Interface`。这一步是后续 `ImplementsInterface()` 与属性类型映射的共同锚点。
- [ ] **P1.2** 📦 Git 提交：`[Interface] Feat: ensure C++ UInterface StaticClass() accessible in script`

- [ ] **P1.3** 注册类型别名（若决策 2 选 B 双重注册）
  - 参考 Patch：`FAngelscriptType::RegisterAlias(InterfaceTypeName, Type)`
  - 使 `UMyInterface`（handle）和 `IMyInterface`（interface）均可在脚本中使用
  - 若决策 2 选 A 或 C 则跳过此步
  - 这一步的背景是：如果同时保留 `U...` 兼容名和 `I...` 语义名，别名注册就不只是便利性问题，而是整个迁移期的兼容阀门。没有它，老脚本可能全部需要改名；有了它，又必须防止别名把 handle 与 interface 语义混淆。
  - 实施时要先确认别名是绑定到“同一个 `UClass*` 的两种 script 名称”，还是“一个 handle type + 一个 interface type”。这两种模型的 `TypeId`、`GetFlags()`、方法表和 `StaticClass()` 语义完全不同，不能在实现时再临场猜。
  - 计划产物里至少要给出 2~3 条对外写法样例：变量声明、Cast、`StaticClass()`、属性类型，让后续实施者能据此写测试与文档，不用再二次设计命名规范。
- [ ] **P1.3** 📦 Git 提交：`[Interface] Feat: register U/I dual name alias for C++ UInterface`

### Phase 2：C++ UInterface 脚本绑定 — 方法注册

> 目标：为 C++ UInterface 的方法注册 AS 调用入口，解决缺口 2（方法未注册）。

- [ ] **P2.1** 实现 C++ 接口方法扫描与注册
  - 参考 Patch 的 `BindNativeInterfaceMethods` 和 `ShouldBindNativeInterfaceFunction`
  - 遍历 C++ 接口 `UClass` 上的 `TFieldIterator<UFunction>`，过滤 `FUNC_BlueprintEvent | FUNC_BlueprintCallable | FUNC_BlueprintPure` 等
  - 为每个方法注册对应的 AS 入口：
    - 若方案 A（模拟）：使用 `RegisterObjectMethod` + `CallInterfaceMethod` + `FInterfaceMethodSignature`
    - 若方案 B（原生）：使用 `RegisterInterfaceMethod`（Patch 的 `RegisterNativeInterfaceMethodDeclaration`）
  - 当前仓库只有脚本声明接口会在预处理阶段把 `InterfaceMethodDeclarations` 注册到 AS 类型；C++ `UINTERFACE` 路径完全没有对等逻辑。这意味着本项是真正补齐“native 接口方法为何在脚本侧不可见”的核心缺口，而不是简单做一次反射遍历。
  - 方法过滤规则必须与现有 Blueprint / ScriptCallable 暴露规则对齐，不能因为是接口就放宽到所有 `UFunction`。至少要说明 editor-only、非脚本可见函数、仅内部 helper 的过滤条件，并明确 `BlueprintNativeEvent` / `BlueprintImplementableEvent` / `ScriptCallable` 是否都进入范围。
  - 实施时要显式覆盖 `AS_USE_BIND_DB` 双路径：当前仓库在 `Bind_BlueprintCallable.cpp` 仍保留 `#if AS_USE_BIND_DB` 分支，本项必须先决定走 DB 预计算声明还是运行时 `FAngelscriptFunctionSignature` 生成声明，不能默认只有一种构建模式存在。
  - 验证除了“方法能调”之外，还要检查声明字符串是否稳定：同一个 native 接口方法不能在多次 bind/hot reload 后重复注册成不同声明，否则后续继承链接和回归测试都会飘。
- [ ] **P2.1** 📦 Git 提交：`[Interface] Feat: register C++ UInterface methods for script access`

- [ ] **P2.2** 实现接口方法继承链接
  - 参考 Patch 的 `InheritNativeInterfaceMembers` + `LinkNativeInterfaceInheritanceRecursive`
  - 确保子接口继承父接口的所有方法（去重）
  - 处理 `UClass::GetSuperClass()` 沿 interface 继承链的递归
  - 当前 `FinalizeClass()` 的 `AddInterfaceRecursive()` 只是在 Unreal 反射层把父接口关系补进 `UClass::Interfaces`，并没有同步把 AS interface 类型的方法表继承链补齐；因此子接口即便在反射层实现了父接口，也不代表脚本类型系统已经能看到全部父接口方法。
  - 若 Phase 0 仍选 A（零 ThirdParty），本项要写清楚模拟方案的做法：到底是注册子接口时把父接口方法声明平铺复制，还是在链接阶段按递归顺序补注册。若选 B/C，则直接对齐 Patch 的 `interfaces` / `methodTable` 继承逻辑，并做去重。
  - 验证重点不是单个接口能调方法，而是子接口在脚本层声明变量时，父接口方法能否直接可见且不会重复出现；这正是后续“继承链”高级测试的判断依据。
- [ ] **P2.2** 📦 Git 提交：`[Interface] Feat: link C++ interface inheritance chain for method visibility`

- [ ] **P2.3** 更新 `Bind_UObject.cpp` 中 `opCast` 的接口分支
  - 参考 Patch 简化写法（逻辑等价但更清晰）：
    ```cpp
    const bool bCanCast = AssociatedClass->HasAnyClassFlags(CLASS_Interface)
        ? Object->GetClass()->ImplementsInterface(AssociatedClass)
        : Object->IsA(AssociatedClass);
    ```
  - 确保 C++ 接口的 `ScriptType->GetUserData()` 能正确解析到 `UClass*`
  - 这一步的重点不是“把代码写短一点”，而是趁接口类型注册补齐后，确认 `opCast` 的判定锚点终于统一成“拿到 AssociatedClass 后判断它是不是接口类”。只有当前置类型注册和 `UserData` 稳定后，这种简化才安全。
  - 改动时要明确保持现有行为不变：普通类仍然走 `IsA`，接口类走 `ImplementsInterface`，失败时返回 `nullptr`。如果 `UserData` 解析为空或解析到错误的 `UClass*`，这里应该把它暴露成测试失败，而不是默默 fallback。
  - 验证至少包含三类对象：实现了接口的 script object、未实现接口的 object、以及普通 native class cast，确保这个重构不会把非接口 cast 路径带坏。
- [ ] **P2.3** 📦 Git 提交：`[Interface] Refactor: simplify opCast interface branch`

### Phase 3：C++ UInterface 测试验证

> 目标：为 C++ UInterface 脚本绑定建立完整测试覆盖。

- [ ] **P3.1** 在 `AngelscriptTest/Shared/` 创建 C++ 测试 UInterface
  - 声明 `UTestNativeInterface` / `ITestNativeInterface`，包含 2-3 个 `UFUNCTION(BlueprintCallable)` 方法
  - 声明 `UTestNativeChildInterface` / `ITestNativeChildInterface`（继承前者），包含 1 个额外方法
  - 更新 `AngelscriptTest.Build.cs` 确保头文件可见
  - 这一步的背景是：当前 `Interface/` 下的 15 个测试几乎都验证“脚本声明接口”闭环，缺少一套可被多个测试文件复用的 native 接口 fixture。若不先固定共享 fixture，后续 `CppInterface.*`、`FInterfaceProperty.*`、签名校验测试会各自复制一套 `UINTERFACE`，导致边界漂移。
  - fixture 设计时不要只放接口声明本身，还要至少准备一个可复用的宿主对象类型（例如 native actor/component 或 script-host base），这样后续测试既能验证“脚本类实现 C++ 接口”，也能验证“native 对象持有接口属性/返回接口对象”。
  - `AngelscriptTest.Build.cs` 这里只确认测试模块能看到新头文件与现有 runtime 公开头即可；不要因为看到 `ThirdParty` 字样就重复追加已存在的公共 include path。`Plan_NativeAngelScriptCoreTestRefactor.md` 已经证明当前依赖传播足够覆盖 `ThirdParty/angelscript/source`。
  - 验证结果应包括：fixture 头可被多个测试文件复用、编译不过度依赖实现细节、接口继承链在反射层可见（`CLASS_Interface` + `GetSuperClass()` 正确）。
- [ ] **P3.1** 📦 Git 提交：`[Test/Interface] Feat: add C++ UInterface test fixtures`

- [ ] **P3.2** 创建 `Interface/AngelscriptCppInterfaceTests.cpp`，覆盖 7 个基础用例 + 1 个双重注册兼容用例（若决策 2 选 B）
  - `CppInterface.RegisterType` — C++ 接口在 AS 中作为类型可见（编译含该类型变量的脚本）
  - `CppInterface.StaticClass` — 脚本中 `UTestNativeInterface::StaticClass()` 非空
  - `CppInterface.Implement` — 脚本类声明实现 C++ 接口，编译成功
  - `CppInterface.ImplementsInterface` — C++ 侧 `ImplementsInterface(UTestNativeInterface::StaticClass())` 返回 true
  - `CppInterface.CastSuccess` — `Cast<UTestNativeInterface>(ScriptActor)` 非空
  - `CppInterface.CastFail` — 未实现该接口的对象 Cast 返回 nullptr
  - `CppInterface.CallMethod` — 通过接口引用调用方法，验证脚本实现被执行并返回正确值
  - `CppInterface.DualNameCompatibility` — 若决策 2 选 B，验证 `UTestNativeInterface` 与 `ITestNativeInterface` 在声明、Cast、`StaticClass()` 上各自承担既定语义，避免双重注册后两种写法互相污染
  - 这组测试是“从 0 到 1 建立 C++ UInterface 基线”的最小闭环，因此既要覆盖 compile-time（类型可见、脚本可声明实现），也要覆盖 runtime（Cast、调用、`ImplementsInterface`）。不要把所有断言都堆到一个大场景里，否则失败时很难判断是哪层出了问题。
  - 用例安排建议严格沿能力链条排序：先验证类型注册，再验证 `StaticClass()`，再验证实现声明，然后才进入 runtime cast / call。这样一旦早期能力没接通，后续失败就能直接定位到 Phase 1 还是 Phase 2。
  - 每个用例都要在命名和脚本源码中明确区分“这是 native 接口”而不是当前已有的脚本接口场景，避免和 `AngelscriptInterfaceAdvancedTests.cpp` 里现有 `CppInterface` 命名造成语义重叠。
  - 验证方式不仅是自动化测试通过，还要检查失败信息是否可读：例如实现缺方法时应该报接口名/方法名，而不是只给出底层 AS compile error。
- [ ] **P3.2** 📦 Git 提交：`[Test/Interface] Feat: add C++ UInterface binding scenario tests (7 cases)`

- [ ] **P3.3** 创建 `Interface/AngelscriptCppInterfaceAdvancedTests.cpp`，覆盖高级场景
  - `CppInterface.ChildInterface` — 脚本类实现 C++ 子接口，自动满足父接口 `ImplementsInterface`
  - `CppInterface.MissingMethod` — 脚本类声明实现 C++ 接口但缺少方法，编译报错
  - `CppInterface.MultipleNativeInterfaces` — 脚本类同时实现多个 C++ 接口
  - `CppInterface.MixedInterfaces` — 脚本类同时实现 C++ 接口和脚本接口
  - `CppInterface.HotReloadRebind` — 反复 full reload / rebind 后，native 接口类型和方法声明不重复注册、不漂移、原有 Cast/调用语义保持稳定
  - 高级测试的职责不是重复基础场景，而是专门守住最容易在后续重构中被打穿的组合边界：接口继承链、缺方法诊断、多接口组合、native + script 混合实现。只有这些点稳定，后续 `FInterfaceProperty` 与签名校验才能放心叠上去。
  - `MissingMethod` 用例要优先验证错误口径，而不是只验证“失败了”。建议断言错误信息中包含类名、接口名和缺失方法名，这样 Phase 5 改签名校验时也能复用同一错误输出风格。
  - `MixedInterfaces` 需要明确 native 接口与脚本接口的注册顺序、方法冲突处理和回归期望，避免后续有人把它误解释成“脚本 interface 已经原生支持”。
  - 若 Phase 0 最终选择 B/C，本组测试还应多保留一条“子接口方法在脚本类型层可见”的断言，用于守住 `InheritNativeInterfaceMembers` 或等价模拟逻辑。
- [ ] **P3.3** 📦 Git 提交：`[Test/Interface] Feat: add C++ UInterface advanced scenario tests`

- [ ] **P3.4** 运行全部接口测试，确认通过并回归
  - 运行 `Angelscript.TestModule.Interface.*`（现有 15 个）
  - 运行 `Angelscript.TestModule.CppInterface.*`（新增 12+ 个，取决于是否启用双重注册兼容用例）
  - 这一步不是形式化收尾，而是给 Phase 4/5 提供“native interface 已稳定”的回归基线。后续任何接口属性或签名校验改动，只要这组基线先不绿，就不应继续扩 scope。
  - 除了新增 `CppInterface.*` 过滤器，建议保留对现有 `Angelscript.TestModule.Interface.*` 的整组回归，确保 C++ interface 支持不会反向破坏脚本接口的旧闭环。
  - 若仓库当前存在单独的 compile-pipeline / hot reload 接口测试入口，也应在这里顺手记录为推荐附加回归，避免后续实施者只跑最窄的 happy path。
- [ ] **P3.4** 📦 Git 提交：`[Test/Interface] Test: verify all interface tests pass including C++ binding`

### Phase 4：FInterfaceProperty 支持

> 目标：实现 `TScriptInterface<T>` 属性类型在脚本中的读写支持。

> 首批范围只覆盖**标量接口属性 + 参数/返回值桥接**；`TArray<TScriptInterface<...>>`、`TSet<>`、`TMap<>` 等容器形态默认不在本 Phase，除非 Phase 0 明确把它们一并纳入范围。

- [ ] **P4.1** 扩展 `Bind_BlueprintType.cpp` 中 `FUObjectType` 的 `CreateProperty`
  - 当 `AssociatedClass` 带 `CLASS_Interface` 时，创建 `FInterfaceProperty` 而非 `FObjectProperty`
  - 参考 Patch 实现：`new FInterfaceProperty(...)` + `SetInterfaceClass(AssociatedClass)`
  - 当前 `FUObjectType::CreateProperty()` 的真实行为非常明确：除了 `UClass::StaticClass()` 特判之外，所有 UObject 相关类型都落成 `FObjectProperty`。这正是 `TScriptInterface<T>` 无法成立的根因之一，因此本项必须优先把“什么时候创建 `FInterfaceProperty`”钉死。
  - 实施时不能只看 `Class != nullptr`，还要覆盖 `Usage.ScriptClass -> GetUserData()` 能解析到 interface `UClass*` 的路径；否则脚本类分析期/热重载期仍可能错误地落回 `FObjectProperty`。
  - 若 Phase 0 选择延后 FInterfaceProperty，本项要在计划正文中显式标注 `N/A until Phase 4 starts`，而不是让后续实施者误以为这里只是一个轻量重构。
  - 验证方式至少要包括一次生成后的反射断言：目标 `UPROPERTY` 真正是 `FInterfaceProperty`，其 `InterfaceClass` 指向预期 `UClass`，而不是只从脚本侧看“能编译”。
- [ ] **P4.1** 📦 Git 提交：`[Interface] Feat: emit FInterfaceProperty for interface type UPROPERTY`

- [ ] **P4.2** 扩展 `FUObjectType::MatchesProperty` 以匹配 `FInterfaceProperty`
  - 参考 Patch：`CastField<FInterfaceProperty>(Property)` → 检查 `InterfaceClass` 一致性
  - 当前 `MatchesProperty()` 只接受 `FObjectProperty`，并在 script class 尚未有真实 `UClass` 时退回按名字比较；这套逻辑若不先插入 `FInterfaceProperty` 分支，会让接口属性在查询阶段永远“不匹配”，进而影响类型推导、getter/setter 生成与 Blueprint 反射映射。
  - 实施时应把 `FInterfaceProperty` 检测放在 `FObjectProperty` 之前，避免接口属性被当成一般对象属性提前吞掉。同时要明确比较的是 `InterfaceClass`，不是 `PropertyClass`。
  - 验证目标是：C++ 原生声明的接口属性、脚本生成的接口属性、以及仍然是普通对象属性的旧路径三者都能被正确区分，不出现误判。
- [ ] **P4.2** 📦 Git 提交：`[Interface] Feat: match FInterfaceProperty in type system`

- [ ] **P4.3** 扩展 `FUObjectType::SetArgument` 和 `GetReturnValue` 以处理 `FScriptInterface` 桥接
  - `SetArgument`：从 `FScriptInterface*` 读取 `GetObject()` 传入 AS
  - `GetReturnValue`：将 AS 返回的 `UObject*` 包装为 `FScriptInterface`（`SetObject` + `SetInterface(GetInterfaceAddress)`）
  - 参考 Patch 的 `FScriptInterface` 桥接模式
  - 当前 `SetArgument()` / `GetReturnValue()` 只在 UE 反射边界和 AS VM 之间传 `UObject*`，这对普通对象没问题，但对接口属性会丢掉 `FScriptInterface` 的双指针语义。尤其 native implementation 的 `InterfacePointer` 可能与 `ObjectPointer` 不同，这一点必须在计划里提前写出来，避免后续实现者沿用“两个指针总是相同”的错误假设。
  - 实施顺序应先打通 `SetArgument`（从反射入 VM），再打通 `GetReturnValue`（从 VM 回反射），最后再看 getter/setter 辅助函数。这样一旦桥接逻辑有错，能更快定位是在入参、返回值还是属性访问器层。
  - 验证时至少要覆盖三类值：空接口、脚本实现接口对象、native 实现接口对象。只有 native case 能证明 `GetInterfaceAddress(InterfaceClass)` 的偏移处理真的正确。
- [ ] **P4.3** 📦 Git 提交：`[Interface] Feat: bridge FScriptInterface in argument and return value paths`

- [ ] **P4.4** 扩展 `FUObjectType::BindProperty` 为接口属性生成 Get/Set 访问器
  - Getter：通过 `FScriptInterface::GetObject()` 返回 `UObject*`
  - Setter：将 `UObject*` + `ImplementsInterface` 验证后写入 `FScriptInterface`
  - 参考 Patch 在 `Bind_Helpers.h` 中新增的 4 个 helper 函数：
    - `GetInterfaceObjectFromProperty`
    - `GetValueFromPropertyGetter_InterfaceHandle`
    - `SetInterfaceObjectFromProperty`
    - `SetInterfaceObjectFromPropertySetter`
  - 当前 `BindProperty()` 的常规路径会根据 `DBProp` 和 `FAngelscriptTypeUsage` 选择 getter/setter helper，但仓库里还没有任何接口属性专用 helper；因此本项的实质是给“接口属性访问器”建立一条独立于普通对象属性的安全路径。
  - Setter 侧必须把 `ImplementsInterface()` 校验写入计划目标，而不是实现时再临场补；否则最容易出现“对象能塞进去、调用时才炸”的延迟错误。若对象不实现目标接口，要么清空属性，要么给出明确诊断，不能静默写入错误状态。
  - Getter / Setter helper 建议按“直接 property 读写”和“Blueprint getter/setter 函数路径”分别列出，防止后续只补了直读直写分支，漏掉 `BlueprintGetter` / `BlueprintSetter` 的 UE 反射通道。
  - 验证至少要包含：直接 `UPROPERTY`、自定义 getter、自定义 setter 三种接口属性读取/写入场景。
- [ ] **P4.4** 📦 Git 提交：`[Interface] Feat: generate Get/Set accessors for interface properties`

- [ ] **P4.5** 扩展 TypeFinder 以识别 `FInterfaceProperty`
  - 参考 Patch 在 `BindUClassLookup` 中新增的 TypeFinder lambda
  - `CastField<FInterfaceProperty>(Property)` → `FAngelscriptTypeUsage::FromClass(InterfaceClass)`
  - 使 C++ 侧声明的 `TScriptInterface<IFoo>` 属性自动映射到 AS 接口类型
  - `BindUClassLookup()` 是当前 Blueprint/UObject 属性反查的重要入口，本项如果只在 `FUObjectType` 层实现而不补 TypeFinder，C++ 已存在的接口属性仍然无法自动映射成 AS 类型，最终会表现为“新建脚本属性可用，但原生类上的接口属性不可见”的半闭环状态。
  - 落实时要先确认 TypeFinder 的分支顺序：`FInterfaceProperty` 应优先于 `FObjectProperty`、`FClassProperty` 等对象系属性类型，否则会被更宽泛的规则误判。若决策 2 选双重注册，还要写清楚 TypeFinder 默认回的是 handle 名还是 interface 名。
  - 验证目标是自动映射，而不是手动声明绕过：至少要选一个 C++ 侧真实声明的 `TScriptInterface<IFoo>` 属性，证明在不额外写绑定代码的情况下也能被脚本正确识别。
- [ ] **P4.5** 📦 Git 提交：`[Interface] Feat: extend TypeFinder to recognize FInterfaceProperty`

- [ ] **P4.6** 创建 `Interface/AngelscriptInterfacePropertyTests.cpp`，覆盖：
  - `InterfaceProperty.Get` — 读取 C++ 类上的 `TScriptInterface<T>` 属性
  - `InterfaceProperty.Set` — 设置接口属性，赋值实现了接口的对象
  - `InterfaceProperty.SetInvalid` — 赋值未实现接口的对象，验证安全处理
  - `InterfaceProperty.CallThrough` — 通过属性获取的接口引用调用方法
  - `InterfaceProperty.Null` — 空引用的安全处理
  - `InterfaceProperty.WithGetter` — 带自定义 Getter 的接口属性
  - `InterfaceProperty.WithSetter` — 带自定义 Setter 的接口属性
  - `InterfaceProperty.ArgBridge` — 接口值作为函数入参经过 `FScriptInterface -> UObject*` 桥接进入脚本后仍可正确调用
  - `InterfaceProperty.ReturnBridge` — 脚本返回接口对象后可稳定桥接回 `FScriptInterface`，并保留正确的对象/接口指针语义
  - 这组测试要专门守住 `FScriptInterface` 的桥接边界，而不是重复前面 `CppInterface.*` 的 Cast/调用测试。换句话说，重点是“接口对象作为属性流转是否稳定”，不是“接口本身是否存在”。
  - `SetInvalid` 必须是硬性用例，因为接口属性最容易在这里出现“写入成功但后续调用崩”的延迟错误。计划里要明确最终期望：是拒绝赋值并给诊断，还是把属性重置为空。
  - 建议至少增加一个 native implementation case 和一个 script implementation case，确保属性桥接不会只在其中一种实现模型上成立。
  - 若仓库后续决定把 `TScriptInterface` 在脚本侧仍呈现为 `UObject*`，则这组测试要把“脚本侧看到的类型名/调用方式”写清楚，避免文档与真实行为脱节。
- [ ] **P4.6** 📦 Git 提交：`[Test/Interface] Feat: add FInterfaceProperty property and bridge tests`

- [ ] **P4.7** 运行全部接口测试 + 回归现有测试，确认通过
  - 本项建议把回归分成两层：先跑 `InterfaceProperty.*` 与 `CppInterface.*` 目标用例，再跑整组 `Angelscript.TestModule.Interface.*`。若第一层不过，不要直接跳到全量回归里淹没信号。
  - 如果 `FInterfaceProperty` 的改动触及 `BindUClassLookup` 或通用 `FUObjectType` 路径，还应追加一轮对象属性/BlueprintType 相关 smoke test，防止把普通对象属性映射打坏。
- [ ] **P4.7** 📦 Git 提交：`[Interface] Test: verify FInterfaceProperty support and regression`

### Phase 5：方法签名校验增强

> 目标：增强方法签名匹配，从仅名字匹配升级为参数类型校验。

- [ ] **P5.1** 改进 `FinalizeClass` 中的方法完整性校验
  - 当前仅 `FindFunctionByName` 同名匹配，改为额外校验参数数量和类型
  - 对照接口 UFunction 存根的 `FProperty` 链与实现类 UFunction 的 `FProperty` 链
  - 若 UFunction 存根为 minimal（无 `FProperty`），先跳过签名校验（不退化现有行为）
  - 当前 `FinalizeClass()` 的真实限制是：它只能通过 `FindFunctionByName` + “不是接口 stub”来判断实现是否存在，完全没有参数/返回类型层面的保证。因此本项的首要目标是把“同名异签也能过”的风险转成显式诊断。
  - 本 Phase 默认同时覆盖“脚本类实现脚本接口”和“脚本类实现 native C++ UInterface”两条签名校验路径；若最终因方案 A/B 的数据来源限制只能先覆盖脚本接口，则必须在 `已确定决策` 中明确把 native mismatch 校验标记为延后项，不能隐式遗漏。
  - 校验逻辑不要停留在“参数数量相同”这一层；至少要在计划里明确比较参数顺序、类型、返回类型，以及 `out/ref/const` 这类反射层可见标记。否则实现者很容易只补半套校验，导致文档宣称“支持签名校验”但实际守不住关键差异。
  - 错误输出也要先写清楚：推荐输出“类名 + 接口名 + 期望声明 + 实际声明”四要素，而不是底层 `FindFunctionByName` 失败式报错。这样 P3 的 `MissingMethod` 和本阶段的签名错误能形成统一诊断风格。
- [ ] **P5.1** 📦 Git 提交：`[Interface] Feat: add parameter signature validation in FinalizeClass`

- [ ] **P5.2** 改进接口 UFunction 存根生成质量
  - 在 `DoFullReloadClass` 中，将 minimal UFunction（仅 `FuncName`）升级为带完整 `FProperty` 参数链
  - 从 `InterfaceMethodDeclarations` 解析参数类型并创建对应 `FProperty`
  - 确保 full reload 和新建类两条路径一致
  - `P5.1` 能否真正生效，取决于接口 stub 自身是否具备足够完整的参数信息；而当前 `DoFullReload()` 的接口路径只解析方法名，不生成任何 `FProperty` 链。这意味着本项不是优化项，而是 Phase 5 的基础设施。
  - 若决策 1 最终选择 C，则必须在本项或 Phase 6 明确“谁是接口签名的单一事实来源”：推荐以 AS compiler/builder 接收到的 interface 声明为 canonical source，`InterfaceMethodDeclarations` 仅作为 UE 侧元数据镜像与诊断辅助；不能让预处理器抽取结果与编译器实际解析结果并行漂移。
  - 实施时要优先复用现有的类型解析/函数签名基础设施，避免再手写一套简陋的字符串 parser。计划里应明确查找是否能借用 `FAngelscriptFunctionSignature`、现有 property/type helper 或 class analyze 阶段的解析结果来生成 stub 参数链。
  - 还要明确 full reload 与初次生成的一致性：如果只有 full reload 生成完整签名、首次编译仍是 minimal stub，Phase 5 的行为会在不同路径下不一致，测试也会很难稳定。
  - 验证不仅是 stub 上有 `FProperty`，还要确认这些 property 的顺序、名字、返回值标记能被 `TFieldIterator<FProperty>` 稳定遍历。
- [ ] **P5.2** 📦 Git 提交：`[Interface] Feat: generate full-signature UFunction stubs for interface methods`

- [ ] **P5.3** 创建 `Interface/AngelscriptInterfaceSignatureTests.cpp`，覆盖：
  - `Signature.Match` — 正确签名实现通过
  - `Signature.ArgCountMismatch` — 参数数量不同报错
  - `Signature.ArgTypeMismatch` — 参数类型不同报错
  - `Signature.ReturnTypeMismatch` — 返回类型不同报错
  - `NativeSignature.ArgTypeMismatch` — 脚本类实现 C++ native 接口时，参数类型不匹配给出显式诊断（若本阶段明确延期，则改为记录在文档中的 deferred case）
  - 这些测试的作用是给 Phase 5 建立真正的红绿边界：以前“同名即可过”的场景，现在必须被拆成“完全一致通过”和“细分 mismatch 失败”。
  - 建议额外在实现说明中预留 `ref/out` 或默认参数差异这类后续扩展位，即便首批不实现，也要在文档里标记“本阶段先覆盖参数个数/类型/返回值，剩余签名细节后续再扩”。这样不会让计划看起来像已经覆盖全部函数签名语义。
  - 断言要关注错误信息可读性，确保 mismatch 时能分辨到底是哪一类差异；否则回归失败时仍然难以定位。
- [ ] **P5.3** 📦 Git 提交：`[Test/Interface] Feat: add interface signature validation tests`

- [ ] **P5.4** 回归全部接口测试，确认通过
  - 这轮回归应明确包含旧的 15 个脚本接口用例与新的 `CppInterface.*` / `Signature.*`。原因是签名校验最容易误伤当前脚本接口模拟路径，尤其当一些旧接口 stub 仍是 minimal 形式时。
  - 如果为了兼容旧路径而引入“没有完整 `FProperty` 就跳过校验”的回退规则，这里要专门保留一个 regression case 证明该回退没有把明显的签名错误也放过去。
- [ ] **P5.4** 📦 Git 提交：`[Interface] Test: verify signature enhancement regression`

### Phase 6：预处理器增强（若采用 Patch 式接口保留）

> 目标：若决策 1 选 B/C（修改 ThirdParty），增强预处理器使接口块进入 AS 编译器。仅当选方案 B/C 时执行此 Phase，否则标记 N/A；若决策 1 最终选 C，则本 Phase 在实施顺序上前置于任何依赖原生 interface 编译路径的 Phase 1/2/5 工作，不能后置为纯收尾项。

- [ ] **P6.1** 修改预处理器不再擦除接口块
  - 接口块内容保留给 AS 编译器原生处理
  - 仍需提取 `InterfaceMethodDeclarations` 供 ClassGenerator 使用
  - 参考 Patch 在 `DetectClasses` / `AnalyzeClasses` / `ParseIntoChunks` 中对 `EChunkType::Interface` 的处理
  - 当前 `AngelscriptPreprocessor.cpp` 的真实行为是：抽取方法声明后把整个 interface chunk 替换为空格。只要这一步不改，脚本 `interface` 就永远不可能进入 AngelScript 编译器原生 interface 路径。因此本项是 B/C 路线最关键的前置条件，而不是一个可有可无的“预处理器优化”。
  - 若决策 1 最终选 C，本项必须同时定义签名事实来源：推荐将 compiler/builder 真正看到的 interface 声明作为 canonical source，`InterfaceMethodDeclarations` 仅保留为 UE 类生成和诊断镜像；一旦两者不一致，以 compiler 路径为准并要求同步修正预处理镜像逻辑。
  - 落实时要明确保留与剥离的边界：保留 interface 关键字与方法体给 AS 编译器；继续提取 `InterfaceMethodDeclarations` 仅用于 UE 侧 UClass/UFunction 生成；只剥离那些会干扰 AS 语法的 UE 宏/注解或 C++ 父类信息，不能再次把整个块抹掉。
  - 本项必须与 compile-pipeline 审计测试绑定执行：至少要证明新语法从 `CompileAnnotatedModuleFromMemory()` / `BuildModule()` 两条路径都能原样到达 parser，而不是在前置管线被吞掉。
- [ ] **P6.1** 📦 Git 提交：`[Interface] Feat: preserve interface blocks for AS compiler`

- [ ] **P6.2** 应用 `as_builder.cpp` 的两处修改（若决策 1 选 C）
  - `CreateDefaultDestructors`：跳过接口类型 + null 检查
  - `DetermineTypeRelations`：当脚本类继承 interface 时调 `AddInterfaceToClass`
  - 使用 `//[UE++]` / `//[UE--]` 标记，记录到 `Documents/Knowledges/AngelscriptChange.md`
  - 这一步只在决策 1 明确选 C 时执行；若最终只需要 `RegisterInterface` 和 native interface 语义，但不需要脚本声明 interface 走完整 builder 继承链，则应明确标注 `N/A`，不要把 `as_builder.cpp` 也机械纳入范围。
  - 两处修改要拆开理解：`CreateDefaultDestructors` 解决“接口不该生成析构”的 builder 假设；`DetermineTypeRelations` 才是把 `class Foo : Bar, IInterface` 纳入原生接口关系图的关键。计划里必须写清楚两者的职责不同，避免后续实施者觉得“只改一处也许就够了”。
  - 由于这是最容易与未来 `2.38` source replacement 冲突的区域之一，本项除了 `[UE++]` / `[UE--]` 标记，还应在 `Documents/Knowledges/AngelscriptChange.md` 中记下 patch 来源、上游函数名和预期移除条件。
- [ ] **P6.2** 📦 Git 提交：`[ThirdParty] Feat: AS builder interface inheritance and destructor handling`

- [ ] **P6.3** 处理隐式 UObject 基类（脚本类只列接口作为父类时）
  - 参考 Patch 的 `bImplicitObjectBaseFromInterfaces` 逻辑
  - 当 `class Foo : IBar` 时自动设为 `UObject` 基类 + 实现 `IBar`
  - 这项的背景是：一旦 interface 块真的进入 AS 编译器，脚本作者就可能写出只有接口继承、没有显式 UObject 基类的声明；而 UE 侧类生成依然需要一个可实例化、可反射的 UObject 基类。若不提前定义这条规则，B/C 路线会在语法层看似成功、生成层却卡死。
  - 落实时要先决定隐式基类策略是否统一固定为 `UObject`，还是根据现有宿主类型（如 actor/component）另有规则。若只支持 `UObject`，必须在文档中明确这条限制，避免用户误以为任意 interface-only 声明都能自动推导出 actor 基类。
  - 验证应至少覆盖 compile success、生成出的 `UClass` 基类正确、以及后续 `ImplementsInterface`/实例化路径不被破坏。
- [ ] **P6.3** 📦 Git 提交：`[Interface] Feat: implicit UObject base for interface-only inheritance`

- [ ] **P6.4** 回归全部接口和非接口测试，确认无引入失败
  - 由于 Phase 6 会碰预处理器和 AngelScript builder/compiler 入口，这里的回归范围必须显著大于 `Interface.*`。至少要额外覆盖一轮 parser/compiler pipeline 与若干非接口脚本测试，防止“为了 interface 改动 parser，结果把普通 class/struct 流程也打坏”。
  - 若 Phase 6 只在方案 B/C 下执行，则这里要把方案 A 的旧基线结果记录下来，作为新旧路线对比的稳定参照。
- [ ] **P6.4** 📦 Git 提交：`[Interface] Test: verify preprocessor enhancement regression`

### Phase 7：文档与测试指南同步

> 目标：更新相关文档，明确接口功能的支持范围和使用方法。

- [ ] **P7.1** 在 `Documents/Knowledges/` 下创建 `InterfaceBinding.md` 知识文档
  - 脚本语法示例（声明、实现、Cast、属性、C++ 接口绑定）
  - 支持的接口功能完整列表
  - 已知限制（AS 语言级 interface 不支持等）
  - 架构决策记录（选了哪个方案、为什么）
  - 与 Patch 方案 / UEAS2 的差异说明
  - 这份知识文档不是计划摘要，而是“功能完成后用户/维护者该看什么”的最终入口，所以内容必须围绕使用边界与行为说明组织，而不是把 Phase 清单原样复制过去。
  - 由于 `Documents/Knowledges/` 已经存在其它知识文档，本项还应顺手补一份接口专题的固定结构：`语法示例 / 支持边界 / native interface / 属性桥接 / 限制 / 决策记录 / 参考资料`。这样后续维护不会再把知识文档写成一次性会议纪要。
  - 若 Phase 0 最终选择了 ThirdParty 路线，本项还要同步链接或摘要 `Documents/Knowledges/AngelscriptChange.md` 中与接口相关的 ThirdParty 变更，明确哪些能力依赖本地 patch，哪些是插件层逻辑。
- [ ] **P7.1** 📦 Git 提交：`[Docs] Feat: add InterfaceBinding knowledge document`

- [ ] **P7.2** 更新 `Documents/Guides/Test.md`，补充接口测试分类
  - 现有 15 个脚本接口测试说明
  - 新增 C++ 接口绑定测试说明
  - 新增 FInterfaceProperty 测试说明
  - 测试命名规范
  - 这一步要解决的是“后来者不知道该跑哪组接口测试、每组测试在守什么边界”的问题，而不是简单补几条文件名。建议把 `Script Interface`、`CppInterface`、`InterfaceProperty`、`Signature` 四类测试分开写，并各自给出推荐过滤器名。
  - 如果最终有 B/C 路线专属的 compile-pipeline / preprocessor 测试，也要在此处注明其与普通 `Interface.*` 场景测试的区别，防止后续只跑场景测试却漏掉编译链路回归。
  - 文档中应明确：接口测试默认先跑窄过滤器定位问题，再跑整组回归，不建议每次都直接上全量 `Angelscript.TestModule`。
- [ ] **P7.2** 📦 Git 提交：`[Docs] Feat: update test guide with interface test categories`

- [ ] **P7.3** 在 `Agents_ZH.md` 补充接口绑定功能说明
  - 1-2 句简述当前接口支持范围
  - 指向 `Documents/Knowledges/InterfaceBinding.md`
  - AGENTS 类文档的职责不是展开实现细节，而是给后来者一个快速边界感。因此这里只需写“当前支持什么 / 还不支持什么 / 详细文档在哪”，不要把 Patch/Phase 细节塞回 AGENTS。
  - 由于仓库规则要求中文文档优先更新，本项也承担“让项目总纲与知识文档不脱节”的责任。若最终命名规范、ThirdParty 决策或接口属性边界发生变化，这里必须同步反映最终结论。
- [ ] **P7.3** 📦 Git 提交：`[Docs] Chore: mention interface binding in Agents_ZH`

## 验收标准

1. **C++ UInterface 脚本绑定**：C++ 定义的 UInterface 在 AS 脚本中可见，能 Cast、调方法、引用 StaticClass（≥12 个新增基础/高级场景测试通过；若决策 2 选 B，还需覆盖双重注册兼容用例）
2. **FInterfaceProperty 支持**：`TScriptInterface<T>` 标量属性能在脚本中读写并通过接口引用调方法，且参数/返回值桥接稳定（≥9 个新增接口属性/桥接测试通过），或有明确的"延后"决策文档
3. **方法签名校验**：接口方法签名不匹配时编译期给出诊断（≥4 个签名错误测试通过，其中至少说明 native mismatch 是已覆盖还是已明确延后）
4. **回归通过**：现有 15 个 Scenario 接口测试全部回归通过
5. **架构决策文档化**：ThirdParty 修改决策、命名约定、FInterfaceProperty 优先级、`AS_USE_BIND_DB` 路径归属、无效接口赋值策略均有明确记录
6. **知识文档**：`Documents/Knowledges/InterfaceBinding.md` 明确描述支持范围、语法、限制

## 风险与注意事项

### 风险 1：ThirdParty 修改与 AS 2.38 升级冲突

若选方案 B/C 修改 AS 引擎代码，`P7`（AS 2.33→2.38 升级）时需重新合入。`IsInterface()` 在 2.38 中的代码位置可能变化。

**缓解**：修改点使用 `//[UE++]` / `//[UE--]` 标记，记录到 `Documents/Knowledges/AngelscriptChange.md`。决策前对照 `P9-A-Upgrade-Roadmap.md` 和 `P5-C-AS238-Breaking-Changes.md` 评估冲突面，并把“为什么这不是 Build.cs / 外部 SDK 问题而是 vendored source 语义问题”写入决策记录。

### 前置风险：把 ThirdParty 问题误判成依赖接入问题

当前仓库已经 vendored AngelScript 源码，`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` 也已公开 `ThirdParty/angelscript/source` 与根目录 include path。若不了解现状，后续很容易把“是否修改 ThirdParty”误写成“是否要新建 External module / 是否缺少第三方 SDK”。

**缓解**：Phase 0 决策文档中必须先写清楚当前仓库的 ThirdParty 现状：不缺依赖接入，不缺头文件可见性，真正待决策的是是否修改 vendored AngelScript 源码来换取原生接口语义。

### 风险 2：C++ UInterface 名称映射

`FAngelscriptType::GetBoundClassName` 返回的类名可能包含前缀或模块名。Patch 使用 `TypeName[0] = 'I'` 做 U→I 转换，假设首字符一定是 `U`。

**缓解**：Phase 1 中添加 `check()` 验证首字符假设。对非标准命名的接口（如不以 U 开头）有兜底逻辑。

### 风险 2.5：`TScriptInterface` / `FScriptInterface` 语义被过度简化

Blueprint-only implementation、native implementation 与脚本实现对象在接口指针获取方式上并不完全一致；若实现时把 `FScriptInterface` 简化成单一 `UObject*`，很容易得到“能存对象但不能稳定调接口”的半正确状态。

**缓解**：Phase 4 的计划与测试中必须同时覆盖空值、脚本实现、native 实现三类 case，并显式使用 `GetInterfaceAddress(InterfaceClass)` 作为 native pointer 的来源。

### 风险 3：FScriptInterface 双指针布局

UE 的 `FScriptInterface` 持 `ObjectPointer` + `InterfacePointer`。脚本实现（`PointerOffset = 0`）两者相同，C++ native 实现两者不同。

**缓解**：Phase 4 实现时，参考 Patch 的 `GetInterfaceAddress` 模式，始终通过 `UObject::GetInterfaceAddress(InterfaceClass)` 获取正确偏移，不假设 `PointerOffset = 0`。

### 风险 4：BindDB 双路径

Patch 代码中有 `#if AS_USE_BIND_DB` / `#else` 双路径。当前 AngelPortV2 需确认使用哪条路径。

**缓解**：Phase 2 实现前确认 `AS_USE_BIND_DB` 在当前构建中的状态，仅实现对应路径。

### 风险 5：接口注册时机

`BindUClass` 的调用顺序可能导致接口类型在方法注册时尚未可见。Patch 通过两轮遍历解决：先注册所有接口方法，再链接继承。

**缓解**：Phase 2 参考 Patch 的两轮遍历模式（先 `BindNativeInterfaceMethods`，再 `LinkNativeInterfaceInheritance`）。

### 风险 6：文档与实际公开语义漂移

接口绑定同时牵涉 `U...` / `I...` 命名、`StaticClass()`、脚本类型名、属性桥接和 ThirdParty 路线。若 Phase 0 决策冻结后不立即同步到知识文档和 AGENTS 摘要，后续最容易出现“测试按一种语义写、用户文档却按另一种语义写”的双轨状态。

**缓解**：Phase 7 不作为纯收尾项看待；只要 Phase 0 确认关键决策，就在文档中先补最小版结论，后续实现完成后再扩成完整知识文档。

## 依赖关系

```text
Phase 0（架构决策）
  ├─ 方案 A / B：
  │    ↓
  │  Phase 1（C++ UInterface 类型注册）── 依赖 Phase 0 的方案选择
  │    ↓
  │  Phase 2（C++ UInterface 方法注册）── 依赖 Phase 1
  │    ↓
  │  Phase 3（C++ UInterface 测试）── 依赖 Phase 1+2
  │    ↓
  │  Phase 4（FInterfaceProperty）── 依赖 Phase 1+2 的基础设施
  │    ↓                              可与 Phase 5 并行调研
  │  Phase 5（签名校验增强）── 依赖 Phase 3 的回归基线
  │
  └─ 方案 C：
       ↓
     Phase 6（预处理器 + builder 原生 interface 前置）── 依赖 Phase 0
       ↓
     Phase 1 / Phase 2 / Phase 3 / Phase 5
       ↓
     Phase 4（若已决定同期做属性桥接）
       ↓
     Phase 7（文档）── 依赖所有前置 Phase 的结论
```

## 参考文档索引

| 文档 | 位置 | 用途 |
|------|------|------|
| AngelPortV2 UInterface 支持方案分析 | `AngelPortV2-UInterface-Support-Analysis.md`（外部专题文档） | 当前实现的完整分析、已知限制、C++ UInterface 缺口 |
| AngelPortV2 vs Patch 接口方案对比 | `AngelPortV2-vs-Patch-Interface-Comparison.md`（外部专题文档） | 两种架构的优劣势、决策参考 |
| dev-as-interface Patch | `参考-dev-as-interface-interface-improvements.patch`（外部 patch 参考） | Patch 完整实现代码，各层改动参考 |
| AS 2.38 升级路线图 | `P9-A-Upgrade-Roadmap.md`（外部专题文档） | ThirdParty 修改与升级兼容性评估 |
| AS 2.38 Breaking Changes | `P5-C-AS238-Breaking-Changes.md`（外部专题文档） | ThirdParty 修改冲突面评估 |
| UnrealCSharp 接口实现 | `Reference/UnrealCSharp` | 横向参考：双形态映射、FDynamicInterfaceGenerator |
