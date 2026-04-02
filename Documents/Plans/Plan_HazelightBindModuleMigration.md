# Hazelight 绑定模块迁移计划

## 背景与目标

### 背景

当前仓库已经具备插件化的 Angelscript 绑定主干：

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` / `.cpp` 提供了当前主绑定入口，既支持现有手写 `FBind` 静态注册，也仍保留 `AddFunctionEntry()`、`RegisterBinds()`、`BindModuleNames` 等兼容接口。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 现在直接通过 `FAngelscriptBinds::CallBinds(CollectDisabledBindNames())` 执行绑定，说明当前分支的主运行路径已经是插件内静态注册，而不是依赖外部 shard 模块动态装配。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 下已经存在 148 个绑定文件，覆盖 `AActor`、`UWorld`、`Subsystem`、`UMG`、容器、GameplayTags、GameplayAbilities 等一批核心类型。
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/`、`Editor/`、`Shared/` 已经形成现成的验证落点，特别是 `AngelscriptBindConfigTests.cpp` 已经能验证按 bind name 的启用/禁用语义。

与此同时，参考插件源码相对当前仓库还保留了更大规模的绑定分片体系：

- runtime 侧存在 `ASRuntimeBind_0`、`10`、`20`、`30`、`40`、`50`、`60`、`70`、`80`、`90`、`100`、`110`
- editor 侧存在 `ASEditorBind_0`、`10`、`20`、`30`
- 同时有 `AngelscriptNativeBinds`、`AngelscriptNativeBindsEditor` 两个聚合模块，用 `StartupModule()` + `FAngelscriptBinds::RegisterBinds(...)` 批量注册更多 `AddFunctionEntry()` 风格的函数绑定

这些参考模块里包含了当前插件尚未完整吸收的大量 UE 类型和函数可见性，尤其是 Actor/Component/UMG、Navigation/CineCamera/MovieScene、Landscape/LevelSequence/Media、Editor Subsystem，以及大量可选插件家族。

### 目标

本计划的目标不是把参考仓库的 shard 模块结构原样搬进当前插件，而是：

1. 以当前插件的 `AngelscriptRuntime` / `AngelscriptEditor` 为最终交付边界，**把参考插件中有维护价值的绑定能力分批吸收进当前插件**。
2. 保持当前插件化、静态注册优先的架构，不把 `ASRuntimeBind_xx` / `ASEditorBind_xx` 当成新的最终模块结构重新引入。
3. 以“**手写主文件 + Generated 补缺文件**”为迁移原则：已有手写绑定继续作为语义主入口，新迁入缺口尽量落到独立 generated 文件，避免直接覆盖现有大文件。
4. 把绑定迁移拆成可编译、可测试、可回滚的 family 批次，先做低争议主线，再做重依赖和 editor 家族。
5. 在计划中显式写清楚首波纳入范围、延后范围和默认排除范围，避免“把有用的绑定全部迁移过来”变成无限膨胀的长期口号。

## 范围与边界

- 参考源根路径在执行时统一通过 `AgentConfig.ini` 中的参考源配置解析；本计划不写死本机绝对路径。
- 纳入范围：`Engine/Plugins/Angelscript/Source` 或等价参考插件源码中的 runtime/editor 绑定模块、对应依赖、测试落点和文档同步。
- 纳入范围：当前插件内 `AngelscriptRuntime` / `AngelscriptEditor` / `AngelscriptTest` 的源码、测试与文档。
- 不纳入范围：把 shard 模块体系作为当前插件的新长期结构重新恢复；恢复 `BindModules.Cache` 驱动的运行时动态装配；与绑定迁移无直接关系的 Loader、VS Code 工作流、引擎补丁、宿主项目业务代码。
- 不纳入范围：没有明确运行时价值、明显面向一次性测试/自动化、或强依赖窄众编辑器场景的类型在第一阶段直接并入主插件。

## 当前事实状态快照

### 当前插件事实

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
  - 已同时具备 `FBind` 静态注册模式和 `AddFunctionEntry()` 兼容接口
  - 仍保留 `BindModuleNames`、`SaveBindModules()`、`LoadBindModules()` 等 shard 时代遗留 helper
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
  - 当前绑定执行主路径是 `CallBinds(CollectDisabledBindNames())`
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`
  - 已有 148 个绑定文件
  - `Bind_AActor.cpp`、`Bind_UUserWidget.cpp`、`Bind_Subsystems.cpp` 等已经包含较强的手写语义包装和错误处理，不能被参考文件整块覆盖
- `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`
  - 已具备 `AIModule`、`NavigationSystem`、`Landscape`、`UMG`、`EnhancedInput`、`GameplayAbilities`、`GameplayTasks`、`AssetRegistry` 等依赖
  - 尚未形成对 `ControlRig`、`RigVM`、`Niagara`、`IKRig`、`LevelSequence`、`Media*`、`Interchange`、`Paper2D`、`GeometryCache`、`MotionWarping`、`Targeting`、`AudioCapture` 等家族的统一依赖承接
- `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`
  - 已具备 `ToolMenus`、`ContentBrowserData`、`AssetTools` 等 editor 绑定主干依赖
- 当前分支的**主绑定执行路径**仍然是 `CallBinds(CollectDisabledBindNames())`，但 legacy shard 工具链并未完全移除：editor 侧仍保留 `GenerateNativeBinds()` 与相关生成入口，runtime 侧仍保留 `BindModules.Cache` / `BindModuleNames` 兼容读取能力。
- 因此当前真实状态应理解为：**静态注册是主架构，生成 shard/cache 仍是兼容/遗留工具链，不应继续成为目标交付结构**。

### 参考插件事实

- 参考源码中额外存在 `ASRuntimeBind_0..110`、`ASEditorBind_0..30`、`AngelscriptNativeBinds`、`AngelscriptNativeBindsEditor`
- 这些模块统一通过 `StartupModule()` 中的 `FAngelscriptBinds::RegisterBinds(...)` 注册批量 `Bind_*()` 函数
- 单个 `Bind_*()` 文件通常以 `AddFunctionEntry(UClass::StaticClass(), Name, ERASE_METHOD_PTR(...))` 的方式补齐方法可见性
- `ASRuntimeBind_10` 是当前最有价值的主线批次，集中包含 Actor / Component / Mesh / UMG / AI / Chaos / Lighting / Input / Rendering 等高频 runtime 类
- `ASRuntimeBind_20` / `30` 主要覆盖 Navigation、CineCamera、MovieScene、Landscape、LevelSequence、Media
- `ASEditorBind_10` / `20` / `30` 主要覆盖常用 editor subsystem、菜单上下文、资产/网格编辑子系统

## 首波纳入 / 延后 / 排除建议

| 分类 | 家族 | 处理建议 |
| --- | --- | --- |
| 首波纳入 | `ASRuntimeBind_10` 中的 Actor / SceneComponent / Mesh / UMG / 常用 AI / Input / Rendering 补缺 | 直接进入 Phase 3 |
| 首波纳入 | `ASRuntimeBind_20` 中的 Navigation 基础项 | 进入 Phase 3，和当前已具备依赖对齐 |
| 首波纳入 | 当前 GAS 差量缺口（基于现有 GAS 锚点补齐） | 提前进入 Phase 3，不再放到高争议最后 |
| 次波复审 | `ASRuntimeBind_20/30` 中的 CineCamera / MovieScene / LevelSequence / Media | 进入 Phase 4，按缺失依赖重组 |
| 次波复审 | `Interchange`、`GeometryCache`、`DynamicMesh`、`MetaSound`、`MediaPlate`、`AudioCapture`、`Targeting`、`AssetTags` | 进入 Phase 4，按依赖和价值重组 |
| 高争议 | `StateTree`、`GameFeatures`、`Niagara`、`ControlRig`、`RigVM`、`IKRig`、`TakeRecorder` | 进入 Phase 5，仅做差量或明确延后 |
| Editor 主线 | `ToolMenus`、`ContentBrowserData`、常用 Editor Subsystem、菜单上下文 | 进入 Phase 6 |
| Editor 次波复审 | `StaticMeshEditorSubsystem`、`SkeletalMeshEditorSubsystem`、`LevelSequenceEditorSubsystem` 与其他重 editor 依赖类型 | 进入 Phase 6 后半段，先补依赖矩阵再迁 |
| 第一阶段默认排除 | `VR Editor`、`MovieScene FBX ControlRig` 导入/导出设置类、明显测试导向 bind、空模块 `ASRuntimeBind_110` | 不纳入第一阶段 |

## TDD 与批次执行纪律

- 从 Phase 3 开始，所有 runtime/editor 绑定 family 一律按“**先写失败测试 → 再补最小绑定实现 → 再跑目标回归 → 再扩下一批方法**”执行，不允许先整批搬运后再统一补测试。
- runtime family 默认先在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 选择最贴近主题的现有测试文件补失败用例；如果没有合适落点，再新建主题测试文件。
- editor family 默认在 `Plugins/Angelscript/Source/AngelscriptTest/Editor/` 下新建主题测试文件；不要继续把 editor 回归全部堆进 `AngelscriptSourceNavigationTests.cpp`。
- 每个 family 的第一条测试至少要覆盖一个“当前缺失、迁移后应成功”的脚本调用；第二条测试优先覆盖一个边界或失败路径，确保新导入 bind 能进入当前仓库既有的错误诊断与禁用体系。
- 若某 family 依赖当前环境尚未接入的模块，第一步仍然应该先写出被 `Build.cs` / feature gate 阻塞的失败测试或占位测试说明，再决定是纳入当前批次还是回退到审计表中的 `Review` / `Drop`。

## 文件级拆分原则

- 本轮细化到文件级时，**默认不采用“一类一个文件”的机械搬运方式**；优先按三条边界共同决定文件粒度：
  - **目标类型家族**：例如 Actor/Controller/Pawn、基础组件、UMG widgets、Navigation、Landscape、GameplayAbility、EditorSubsystem。
  - **依赖簇**：同一文件内的类应尽量共享同一组 `Build.cs` 依赖，避免为了一个零散类把整个大文件挂到重依赖模块上。
  - **子系统/命名空间边界**：对更适合以 `System` / `Gameplay` / `Widget` / `Niagara` / `EditorSubsystem` 语义理解的能力，优先做成同域文件，而不是按参考 shard 编号原样铺开。
- 已有手写锚点文件继续承担“脚本语义主入口”职责；新增文件更适合命名为“某个家族的 additive/generated gap 文件”，而不是把现有手写大文件无限膨胀。
- 对新增 runtime 文件，优先考虑以下命名风格之一，并在审计表中固定：
  - `Bind_<Family>_Generated.cpp`：用于承接参考 shard 的纯 `AddFunctionEntry()` 式差量迁移。
  - `Bind_<Family>_<Subsystem>.cpp`：用于一组共享依赖、共享主题的类簇，例如 `Bind_Widget_Input.cpp`、`Bind_Navigation_Runtime.cpp`。
  - `Bind_<TargetType>_<Gap>.cpp`：仅在当前已存在强语义主文件、且需要把新增差量和旧手写逻辑强行隔离时使用，例如 `Bind_AActor_Generated.cpp`。
- editor 侧同理，优先按 `EditorSubsystem` / `MenuContext` / `AssetTools` / `MeshEditors` 等语义簇拆分，而不是让 `ASEditorBind_10` 的所有类型平铺成一批互不相关的小文件。
- 任何文件级拆分一旦确定，都必须在 Phase 1 的审计表里固定“参考输入文件集合 → 当前目标文件”的映射，防止后续多人并行执行时再次重组边界。

## 细化到文件的建议清单

> 下表是基于参考 `ASRuntimeBind_*` / `ASEditorBind_*` 文件集合、当前仓库已有手写锚点、以及依赖簇重组后的**建议目标文件**。这些文件名可以直接作为 Phase 1 审计表与后续实施批次的初稿。

### Runtime：Phase 3 依赖已就绪/低成本补齐批次

| 动作 | 目标文件 | 主要承接类 | 说明 |
| --- | --- | --- | --- |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` | `AActor` | 保留当前手写 world context / 错误处理语义，只补 reference `Bind_Actor.cpp` 里的缺失方法 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_PawnCharacterFramework_Generated.cpp` | `APawn`、`ACharacter`、`ADefaultPawn`、`AHUD`、`APlayerState` | 同属基础 Gameplay Framework；当前仓库无独立同名 bind 文件，适合作为一个差量家族文件 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_APlayerController.cpp` | `APlayerController` | 继续作为 PlayerController 的主锚点，按方法级补缺 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_GameModeFramework_Generated.cpp` | `AGameModeBase`、`AGameMode`、`AGameStateBase`、`ALevelScriptActor`、`UGameUserSettings`、`UReplaySubsystem` | 统一吸收 GameMode / GameState / LevelScript 这条 game-flow 主线 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_PlayerCameraRuntime_Generated.cpp` | `APlayerCameraManager`、`UCameraComponent`、`USpringArmComponent`、`UCameraModifier`、`UCameraShakeSourceComponent` | 同属玩家相机/镜头链路，避免散落到多个文件 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp` | `UActorComponent` | 继续作为 component 基础锚点 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp` | `USceneComponent` | 继续作为 transform/attachment 主锚点 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_PrimitiveComponents_Generated.cpp` | `UStaticMeshComponent`、`UInstancedStaticMeshComponent`、`UBoxComponent`、`UCapsuleComponent`、`USphereComponent`、`UArrowComponent`、`UBillboardComponent`、`UDecalComponent` | 这批都属于基础 primitive/shape/visual component，且共享 Engine/UMG 外轻依赖 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_RenderAndCaptureComponents_Generated.cpp` | `UTextRenderComponent`、`UWidgetComponent`、`USceneCaptureComponent2D`、`USceneCaptureComponentCube`、`ASceneCaptureCube`、`URuntimeVirtualTextureComponent`、`URuntimeVirtualTexture` | 同属渲染采集/显示组件 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_EffectsComponents_Generated.cpp` | `UParticleSystemComponent`、`UAudioComponent`、`UForceFeedbackComponent` | 归并为效果输出组件批次 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_MovementComponents_Generated.cpp` | `UCharacterMovementComponent`、`UInterpToMovementComponent`、`UProjectileMovementComponent` | 当前已有 `Bind_UProjectileMovementComponent.cpp` 时优先补缺，否则统一迁入这个差量文件 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AIFramework_Generated.cpp` | `AAIController`、`UBrainComponent`、`UBehaviorTreeComponent`、`UBlackboardComponent`、`UAIPerceptionComponent`、`UAIPerceptionStimuliSourceComponent`、`UEnvQueryInstanceBlueprintWrapper`、`UPawnSensingComponent`、`UAvoidanceManager` | 同属 AIModule 主线能力 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AnimationRuntime_Generated.cpp` | `UAnimInstance`、`UAnimSingleNodeInstance`、`UAnimMontage`、`USkeletalMesh`、`USkeleton`、`USkeletalMeshSocket`、`UPoseAsset`、`UAnimationSettings` | 统一承接动画运行时核心类型 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USkeletalMeshComponent.cpp` | `USkeletalMeshComponent` | 保留手写语义，增量补 reference 中的组件方法 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USkinnedMeshComponent.cpp` | `USkinnedMeshComponent` | 同上 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UPoseableMeshComponent.cpp` | `UPoseableMeshComponent` | 同上 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UMGWidgets_Basic_Generated.cpp` | `UButton`、`UCheckBox`、`UImage`、`UProgressBar`、`USlider`、`UEditableText`、`UEditableTextBox`、`UMultiLineEditableText`、`UMultiLineEditableTextBox`、`UTextBlock`、`UComboBoxString`、`UInputKeySelector` | 基础输入/显示控件一批 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UMGWidgets_Panels_Generated.cpp` | `UCanvasPanel`、`UCanvasPanelSlot`、`UGridPanel`、`UGridSlot`、`UHorizontalBox`、`UHorizontalBoxSlot`、`UVerticalBox`、`UVerticalBoxSlot`、`UOverlay`、`UOverlaySlot`、`UWrapBox`、`UWrapBoxSlot`、`UUniformGridPanel`、`UUniformGridSlot`、`UScrollBox`、`UScrollBoxSlot`、`UBorder`、`UBorderSlot`、`USizeBox`、`USizeBoxSlot`、`UScaleBox`、`UScaleBoxSlot` | 面板/Slot 类统一一批，利于 UMG 布局测试 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UMGWidgets_Advanced_Generated.cpp` | `UBackgroundBlur`、`UCircularThrobber`、`UDynamicEntryBox`、`UExpandableArea`、`UInvalidationBox`、`UListView`、`UMenuAnchor`、`URetainerBox`、`USafeZone`、`USpacer`、`USpinBox`、`UTileView`、`UTreeView`、`UThrobber`、`UViewport`、`UWidgetSwitcher`、`UWidgetSwitcherSlot`、`UWidgetAnimation`、`UUMGSequencePlayer`、`UWidgetInteractionComponent`、`UWindowTitleBarArea`、`UWindowTitleBarAreaSlot` | 高级 UMG 控件单独成批，避免基础 widgets 文件过大 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp` | `UEnhancedInputComponent` | 当前已有文件，继续补 reference 差量方法 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_EnhancedInputRuntime_Generated.cpp` | `UInputMappingContext`、`UEnhancedInputUserSettings`、`UEnhancedInputWorldSubsystem`、`UEnhancedPlayerMappableKeyProfile` | 与现有 EnhancedInput 依赖对齐 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_NavigationRuntime_Generated.cpp` | `UNavigationSystemV1`、`UNavigationPath`、`UNavModifierComponent`、`UNavModifierVolume`、`UNavRelevantComponent`、`URecastNavMesh`、`ANavLinkProxy`、`UCrowdFollowingComponent` | 当前 `NavigationSystem` / `AIModule` 依赖已就绪，适合前置 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp` | GAS library 层入口 | 继续作为 GAS 语义主入口 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayAbilitySpec.cpp` | `FGameplayAbilitySpec` | 继续做值类型/桥接锚点 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayAttribute.cpp` | `FGameplayAttribute` | 继续做属性锚点 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayEffectSpec.cpp` | `FGameplayEffectSpec` | 继续做效果规格锚点 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_GameplayAbilityCore_Generated.cpp` | `UAbilitySystemComponent`、`UGameplayAbility`、`UGameplayAbilityWorldReticle`、`UGameplayCueNotify_Actor`、`UGameplayCueNotify_Static` | 与现有 GAS 锚点配合，避免都塞进 library 文件 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_GameplayAbilityTasks_Generated.cpp` | `UAbilityTask_SpawnActor`、`UAbilityTask_WaitTargetData`、`UAbilityTask_VisualizeTargeting`、`UAbilityTask_ApplyRootMotionJumpForce` | AbilityTask 单独成批更易测试 |
| 修改 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_LandscapeProxy.cpp` | `ALandscapeProxy` | 继续作为 Landscape 手写锚点 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_LandscapeRuntime_Generated.cpp` | `ALandscape`、`ULandscapeComponent`、`ULandscapeHeightfieldCollisionComponent` | 与 `Landscape` 依赖对齐，避免把组件和 proxy 混在一起 |

### Runtime：Phase 4/5 中高争议/重依赖批次

| 动作 | 目标文件 | 主要承接类 | 说明 |
| --- | --- | --- | --- |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_CineCamera_Generated.cpp` | `ACineCameraActor`、`UCineCameraComponent`、`UCineCameraSettings`、`ACameraRig_Rail` | 从 `ASRuntimeBind_20` 拆出单独相机家族 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_LevelSequence_Generated.cpp` | `ULevelSequence`、`ALevelSequenceActor`、`ULevelSequencePlayer`、`ULevelSequenceDirector`、`ULevelSequenceMediaController`、`ULevelSequenceBurnInOptions`、`ATemplateSequenceActor` | 与 Sequencer/LevelSequence 依赖关联 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_MovieScene_Generated.cpp` | `UMovieSceneSubSection`、`UMovieSceneAudioSection`、`UMovieSceneCameraCutSection`、`UMovieSceneCinematicShotSection`、`UMovieSceneLevelVisibilitySection`、`UMovieSceneDataLayerSection`、`UMovieSceneCVarSection`、`UMovieSceneMetaData`、`UMovieSceneParameterSection`、`UMovieSceneComponentMaterialParameterSection` | MovieScene 类型集中处理 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_MediaRuntime_Generated.cpp` | `UMediaPlayer`、`UMediaComponent`、`UMediaSoundComponent`、`UMediaTexture`、`UMediaPlaylist`、`UFileMediaSource`、`UImgMediaSource`、`UMediaPlateComponent` | Media 及其派生组件独立一批 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InterchangeCore_Generated.cpp` | `UInterchangeManager`、`UInterchangePipelineConfigurationBase`、`UInterchangePipelineStackOverride`、`UInterchangeAssetImportData` | 只放 manager/pipeline 入口 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InterchangeNodes_Generated.cpp` | `UInterchangeSourceData`、`UInterchangeSourceNode`、`UInterchangeBaseNode`、`UInterchangeBaseNodeContainer` 等核心 node | 基础 node 与 source data 一批 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InterchangeFactoryNodes_Generated.cpp` | StaticMesh / SkeletalMesh / Material / Texture / Camera / Light / Animation / Scene 等 `UInterchange*FactoryNode` 家族 | factory node 太多，必须单独文件 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_DynamicMesh_Generated.cpp` | `UDynamicMesh`、`UDynamicMeshComponent`、`ADynamicMeshActor`、`UDynamicMeshPool`、`UOctreeDynamicMeshComponent`、`UProceduralMeshComponent`、`UCustomMeshComponent` | DynamicMesh / ProceduralMesh 同域 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_GeometryCache_Generated.cpp` | `UGeometryCacheComponent`、`AGeometryCacheActor` | 几何缓存单独 gate |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_MetaSound_Generated.cpp` | `UMetaSoundBuilderSubsystem`、`UMetaSoundAssetSubsystem`、`UMetaSoundCacheSubsystem`、`UMetaSoundOutputSubsystem`、`UMetaSoundPatchBuilder`、`UMetaSoundSourceBuilder`、`UMetasoundGeneratorHandle`、`UMetasoundParameterPack` | MetaSound builder/asset/output 一批 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AudioSynthesis_Generated.cpp` | `UModularSynthComponent`、`UGranularSynth`、`USynthSamplePlayer`、`USynthComponentMonoWaveTable`、`USynthComponentToneGenerator`、各类 `USourceEffect*Preset`、`USubmixEffect*Preset` | 合成/效果 preset 集中处理 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AudioWidgets_Generated.cpp` | `UAudioMeter`、`UAudioOscilloscope`、`UAudioRadialSlider`、`UAudioVectorscope`、`URadialSlider` | Editor/Runtime 共用的音频 widgets 单独一批 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Paper2D_Generated.cpp` | `UPaperFlipbook`、`UPaperFlipbookComponent`、`UPaperSpriteComponent`、`UPaperGroupedSpriteComponent`、`UPaperTerrainComponent`、`UPaperTileMapComponent` | 2D 家族独立 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_PhysicsChaos_Generated.cpp` | `AChaosSolverActor`、`UChaosDestructionListener`、`UGeometryCollection`、`UGeometryCollectionComponent`、`AGeometryCollectionActor`、`UClusterUnionComponent`、`UChaosClothingInteractor`、`UChaosCacheManager`、`UClothingSimulationInteractorNv` | Chaos/GeometryCollection/Cloth 同域 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_GameplayCameras_Generated.cpp` | `UGameplayCamerasSubsystem`、`UGameplayCameraComponent`、`UCameraAnimationCameraModifier` | GameplayCameras 单独 gate |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_StateTreeGameplay_Generated.cpp` | `UStateTreeComponent`、`UGameFrameworkComponentManager`、`UActorSequenceComponent` | StateTree/Gameplay framework 扩展 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TargetingRuntime_Generated.cpp` | `UTargetingSubsystem`、`UAsyncAction_PerformTargeting`、`UAssetTagsSubsystem` | Targeting 与资产标签一批 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AudioCapture_Generated.cpp` | `UAudioCapture` | 单依赖小文件 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_VariantManager_Generated.cpp` | `ULevelVariantSets`、`ALevelVariantSetsActor`、`UVariant`、`UVariantSet`、`ASwitchActor` | Variant Manager 家族 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_MotionWarping_Generated.cpp` | `UMotionWarpingComponent` | MotionWarping 单独 gate |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ControlRig_Generated.cpp` | `UControlRigComponent`、`UControlRigControlActor`、`UControlRigShapeActor`、`UControlRigBlueprint`、`UControlRigTestData`、`UControlRigPoseAsset`、`UModularRigController` | ControlRig 自成依赖簇 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_RigVM_Generated.cpp` | `URigHierarchy`、`URigHierarchyController`、`URigVMController`、`URigVMGraph`、`URigVMNode`、`URigVMPin`、`URigVMLink`、`URigVMBlueprint`、`URigVMCompiler` 等 | RigVM 单独文件，避免与 ControlRig 相互污染 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Niagara_Generated.cpp` | `ANiagaraActor`、`UNiagaraComponent`、`UNiagaraSimCache`、`UNiagaraParameterCollectionInstance`、`UNiagaraDataChannelReader`、`UNiagaraDataChannelWriter`、`UNiagaraDataInterfaceGrid2DCollection`、`UNiagaraDataInterfaceGrid3DCollection`、`UNiagaraPreviewGrid`、Niagara AnimNotify 家族 | Niagara 主线能力 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_IKRigRuntime_Generated.cpp` | `UIKRigComponent`、`UIKRetargeter` | IKRig runtime 侧 |
| 创建 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TakeRecorderRuntime_Generated.cpp` | `UTakeRecorder`、`UTakeRecorderPanel`、`UTakeRecorderSources`、`UTakeMetaData` | TakeRecorder 单独 gate |

### Editor：Phase 6 建议目标文件

| 动作 | 目标文件 | 主要承接类 | 建议测试文件 |
| --- | --- | --- | --- |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Subsystems/Bind_EditorSubsystems_Core.cpp` | `ULevelEditorSubsystem`、`UEditorActorSubsystem`、`UEditorAssetSubsystem`、`UAssetEditorSubsystem`、`UUnrealEditorSubsystem` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorSubsystemBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Subsystems/Bind_EditorSubsystems_Data.cpp` | `ULayersSubsystem`、`UDataLayerEditorSubsystem`、`UContentBrowserDataSubsystem` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorSubsystemBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Utilities/Bind_EditorUtility.cpp` | `UEditorUtilitySubsystem`、`UEditorUtilityLibrary`、`UEditorValidatorSubsystem`、`UActorGroupingUtils` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorUtilityBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Contexts/Bind_MenuContexts.cpp` | `ULevelEditorContextMenuContext`、`UContentBrowserAssetContextMenuContext`、`UBlueprintEditorToolMenuContext`、`UAssetEditorToolkitMenuContext`、`UPersonaToolMenuContext`、`USubobjectEditorMenuContext`、`URigVMEditorMenuContext`、`UAnimationSequenceBrowserContextMenuContext` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptMenuContextBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Blueprint/Bind_SubobjectEditor.cpp` | `USubobjectDataSubsystem`、`USubobjectDataBlueprintFunctionLibrary` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSubobjectEditorBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Blueprint/Bind_AnimationBlueprintEditor.cpp` | `UAnimationGraph`、`UAnimGraphNode_PoseDriver` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptAnimationBlueprintBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/MeshEditors/Bind_MeshEditorSubsystems.cpp` | `UStaticMeshEditorSubsystem`、`USkeletalMeshEditorSubsystem` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptMeshEditorBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Sequencer/Bind_SequencerEditor.cpp` | `ULevelSequenceEditorSubsystem`、`USequencerCurveEditorObject` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSequencerBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/InputAudio/Bind_EnhancedInputEditor.cpp` | `UEnhancedInputEditorSubsystem` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptInputAudioEditorBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/InputAudio/Bind_MetaSoundEditor.cpp` | `UMetaSoundEditorSubsystem` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptInputAudioEditorBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Niagara/Bind_NiagaraEditorBridge.cpp` | `UUpgradeNiagaraScriptResults`、`UNiagaraPythonScriptModuleInput`、`UNiagaraPythonModule`、`UNiagaraPythonEmitter` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptNiagaraEditorBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/IKRig/Bind_IKRigEditor.cpp` | `UIKRigController`、`UIKRetargeterController` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptIKRigEditorBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Assets/Bind_AssetImportTasks.cpp` | `UAssetImportTask`、`UReimportFbxSceneFactory` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptAssetImportBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Widgets/Bind_EditorWidgets.cpp` | `UAssetThumbnailWidget`、`USinglePropertyView` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptEditorWidgetBindingTests.cpp` |
| 创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Geometry/Bind_GeneratedDynamicMeshActorEditor.cpp` | `AGeneratedDynamicMeshActor` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptGeometryEditorBindingTests.cpp` |
| 可选创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/VREditor/Bind_VREditor.cpp` | `UViewportWorldInteraction`、`UVREditorInteractor`、`AVREditorTeleporter` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptVREditorBindingTests.cpp` |
| 可选创建 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Binds/Sequencer/Bind_ControlRigFBXSettings.cpp` | `UMovieSceneUserImportFBXControlRigSettings`、`UMovieSceneUserExportFBXControlRigSettings` | `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSequencerBindingTests.cpp` |

## 分阶段执行计划

### Phase 1：建立可执行的绑定审计基线

> 目标：先把“参考侧有哪些模块/文件、当前已有什么、哪些应该迁、迁到哪、怎么测”固定成统一审计表，避免后续每一批都重新人工 diff。

- [ ] **P1.1** 建立 reference → current 绑定审计表
  - 以参考插件源码为唯一输入，把 `ASRuntimeBind_0/10/.../100`、`ASEditorBind_0/10/20/30`、`AngelscriptNativeBinds*` 拆成 `Runtime` / `Editor` / `Review` / `Drop` 四类。
  - 每条记录至少包含：参考文件、当前等价文件、计划落点、依赖模块、测试落点、处理结论（迁移 / 延后 / 放弃）。
  - 对当前已经存在的手写锚点必须显式标记，例如 `Bind_AActor.cpp`、`Bind_UUserWidget.cpp`、`Bind_Subsystems.cpp`、`Bind_AngelscriptGASLibrary.cpp`，避免后续直接整文件覆盖。
  - 审计表应优先做成可 diff 的机器可读格式（CSV/Markdown 均可），以便后续每批迁移都能直接筛选文件集合。
- [ ] **P1.1** 📦 Git 提交：`[Binds] Docs: add reference-to-current bind audit matrix`

- [ ] **P1.2** 冻结迁移原则：不恢复 shard 作为最终结构
  - 当前插件已经通过 `CallBinds()` 走静态注册主路径，但 editor 侧仍保留 `GenerateNativeBinds()`，runtime 侧也仍保留 `BindModules.Cache` / `BindModuleNames` 兼容链路；因此参考 shard 只能作为迁移输入，不能继续作为当前插件新的长期模块边界。
  - 明确“手写主文件 + Generated 补缺文件”原则：已有 `Binds/Bind_*.cpp` 保留为语义主入口，新迁入缺口统一落到 runtime/editor 内部的 generated 目录或等价层次，减少对现有大文件的侵入。
  - 对 `AddFunctionEntry()` 风格迁移和 `ExistingClass().Method(...)` 风格迁移给出选择准则：高频类先 additive 补缺，稳定后再按需回写手写文件收口。
- [ ] **P1.2** 📦 Git 提交：`[Binds] Docs: freeze static bind absorption strategy`

### Phase 2：建立吸收骨架与依赖矩阵

> 目标：先把“新绑定文件放哪、如何注册、如何禁用、哪些依赖可直接承接、哪些必须 gate”固定下来，再开始批量搬运。

- [ ] **P2.1** 在当前插件内建立 generated bind 吸收骨架
  - 在 `AngelscriptRuntime` / `AngelscriptEditor` 内建立统一的 generated bind 落点，建议命名为 `Binds/Generated/` 或语义等价目录，并提供一个可控的统一注册入口。
  - 注册入口继续复用当前 `RegisterBinds()` + `AddFunctionEntry()` 组合，而不是重新引入 `ASRuntimeBind_xx` / `ASEditorBind_xx` 模块作为交付物。
  - 新批次的 bind name 需要带 family 标识，例如 `Generated.Runtime10.Actor`、`Generated.Editor10.Subsystems`，以复用当前 `DisabledBindNames`、`GetBindInfoList()` 的可见性和禁用能力。
  - 文件布局要能追溯回参考 family，但不要把所有迁入内容堆到一个大文件里；优先按类族或依赖簇拆开。
- [ ] **P2.1** 📦 Git 提交：`[Binds] Refactor: add generated bind absorption entrypoints`

- [ ] **P2.2** 固定依赖矩阵与 feature gate 规则
  - 先对照 `AngelscriptRuntime.Build.cs` / `AngelscriptEditor.Build.cs`，把每个参考 family 标成“当前已具备依赖”“可低风险新增依赖”“重依赖/可选插件/编辑器专属”三类。
  - 当前明显可直接承接的基础依赖包括 `AIModule`、`NavigationSystem`、`Landscape`、`UMG`、`EnhancedInput`、`GameplayAbilities`、`GameplayTasks`、`AssetRegistry`、`ToolMenus`、`ContentBrowserData` 等。
  - `ControlRig`、`RigVM`、`Niagara`、`IKRig`、`LevelSequence`、`Media*`、`Interchange`、`Paper2D`、`GeometryCache`、`MotionWarping`、`Targeting`、`AudioCapture` 等需独立 gate；任何新增重依赖都必须和对应 bind family 同批进入，而不是先一次性膨胀 `Build.cs`。
- [ ] **P2.2** 📦 Git 提交：`[Binds] Docs: add bind dependency and feature-gate matrix`

- [ ] **P2.3** 收口 legacy shard 工具链与新迁移路径的共存规则
  - 当前仓库仍保留 `GenerateNativeBinds()`、`BindModules.Cache`、`BindModuleNames` 等 legacy 生成/缓存链路，因此必须先决定：本轮迁移期间这些入口是完全禁用、仅保留只读兼容，还是允许作为一次性审计/生成辅助工具存在。
  - 明确要求：新迁入 bind family 不能依赖 `GenerateNativeBinds()` 产物才能生效；如果 legacy cache 文件存在，也不能导致同一 family 同时经过 generated shard 和当前静态入口双重注册。
  - 为此需要补一条策略说明：当发现 legacy generated module 或 cache 命中时，当前阶段应如何处理（忽略 / 清理 / 显式报错 / 迁移后统一移除）。
  - 同时冻结 bind-name 设计规则：family 级别的命名需要既能复用 `DisabledBindNames`，又能避免因为重复/过粗命名导致 `GetAllRegisteredBindNames()` 与 `GetBindInfoList()` 的可见性混乱。
- [ ] **P2.3** 📦 Git 提交：`[Binds] Docs: define coexistence rules for legacy shard tooling`

### Phase 3：吸收低争议 runtime 主线绑定

> 目标：优先吸收最常用、最接近当前主线、且当前依赖已具备或低成本补齐的 runtime 绑定，尽快扩大脚本 API 面。

- [ ] **P3.1** 迁移 `ASRuntimeBind_10` 的 Actor / Component / Mesh / UMG / 常用 AI 主线缺口
  - 先在 `AngelscriptTest/Bindings/` 中为每个准备纳入的高频类补失败测试，再开始吸收 reference 侧的 `AddFunctionEntry()` 缺口；没有失败测试的类不进入本批次。
  - 以方法级 diff 为主，不整文件覆盖 `Bind_AActor.cpp`、`Bind_UUserWidget.cpp`、`Bind_USceneComponent.cpp`、`Bind_USkeletalMeshComponent.cpp` 等已有手写文件。
  - 当前已有手写包装、world-context、错误提示或脚本语义增强的路径继续保留；参考模块只补当前缺失的方法与属性可见性。
  - 新增测试优先落在 `AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp`、`AngelscriptObjectBindingsTests.cpp`、`AngelscriptUtilityBindingsTests.cpp`、`AngelscriptNativeEngineBindingsTests.cpp`，每个高频类至少补一条成功路径和一条边界路径。
- [ ] **P3.1** 📦 Git 提交：`[Binds] Feat: absorb runtime family-10 core engine gaps`

- [ ] **P3.2** 迁移当前依赖已就绪的 Navigation / Landscape / GAS 差量缺口
  - 本批次同样先补失败测试，再按依赖拆分实现；优先只做当前 `Build.cs` 已具备依赖或低成本补齐的部分。
  - Navigation / Landscape 继续采用 additive 补缺；GAS 相关能力以当前已有 `Bind_AngelscriptGASLibrary.cpp`、`Bind_FGameplayAbilitySpec.cpp`、`Bind_FGameplayAttribute.cpp` 等锚点为主，做差量补齐而不是重复导入整套家族。
  - 对 runtime/editor 双边都可能出现的类型，先在审计表里固定唯一落点，避免同一类型重复注册。
  - 对当前已存在的 `Bind_LandscapeProxy.cpp`、GAS 绑定文件和其他手写锚点继续采用 additive 补缺，避免把现有手写逻辑打散。
- [ ] **P3.2** 📦 Git 提交：`[Binds] Feat: absorb dependency-ready runtime gaps`

### Phase 4：按依赖簇迁移中争议 runtime 家族

> 目标：把可选插件和工具型 runtime 家族按“同依赖、同测试路径”的方式重组迁移，而不是继续按参考模块编号机械搬运。

- [ ] **P4.1** 迁移 `CineCamera`、`MovieScene`、`LevelSequence`、`Media` 以及 `Interchange`、`GeometryCache`、`DynamicMesh`、`MetaSound`、`MediaPlate`、`AudioCapture`、`Targeting`、`AssetTags` 等中争议家族
  - 先按依赖簇各自补失败测试或最小占位测试，再决定是否真正引入该依赖；没有测试锚点的家族只能留在审计表里，不能直接开搬。
  - `CineCamera`、`MovieScene`、`LevelSequence`、`Media` 当前不应继续算作低争议首波，而应和其他缺失依赖家族一起按依赖簇重组处理。
  - 这些家族往往跨越多个参考 shard，执行时应按依赖簇重组，而不是保持 `ASRuntimeBind_0/20/80/100` 的原始编号边界。
  - 每一簇都必须先形成最小闭环：`Build.cs` 依赖、generated bind 文件、1~2 条 bindings smoke tests、1 条文档边界说明。
  - 任何明显面向测试、自动化或 editor-only 的类型默认不强塞进 runtime，而是记录到后续 editor/工具阶段。
- [ ] **P4.1** 📦 Git 提交：`[Binds] Feat: absorb medium-risk optional runtime families`

### Phase 5：处理高争议重依赖家族

> 目标：对 ControlRig / RigVM / Niagara / IKRig / TakeRecorder / StateTree 等重依赖家族给出明确的纳入或延后结论，不让 backlog 长期悬空。

- [ ] **P5.1** 评估并迁移高争议 runtime 家族的差量缺口
  - 每个高争议家族都先写失败测试或 capability gate 测试，证明当前缺口真实存在，再决定进入“差量迁移”还是“明确延后”。
  - 当前仓库已经有 `Bind_AngelscriptGASLibrary.cpp`、`Bind_FGameplayAbilitySpec.cpp`、`Bind_FGameplayAttribute.cpp` 等 GAS 锚点，因此 GAS 相关能力必须优先做“差量补齐”，不能按全新家族重复导入。
  - `StateTree`、`GameFeatures`、`Niagara`、`ControlRig`、`RigVM`、`IKRig`、`TakeRecorder` 等应逐族确认：依赖是否存在、是否需要 editor-only 模块、测试环境是否可稳定运行、与当前手写绑定是否冲突。
  - 对暂不纳入的家族要在审计表和阶段文档里给出明确原因，不允许只写“以后再看”。
- [ ] **P5.1** 📦 Git 提交：`[Binds] Docs: classify high-risk runtime families by outcome`

### Phase 6：吸收 editor binding 家族

> 目标：在当前 `AngelscriptEditor` 模块内补齐有价值的 editor 绑定，而不是恢复 `AngelscriptNativeBindsEditor` 作为新的交付模块。

- [ ] **P6.1** 迁移 `ASEditorBind_0/10/20/30` 的 Editor Subsystem / 菜单上下文 / 资产与网格编辑器主线能力
  - editor 绑定同样先补失败测试，再迁实现；最先落地的测试建议直接覆盖 `ToolMenus` / `ContentBrowserData` / `EditorSubsystem` 三类基础路径。
  - 优先吸收与当前 `AngelscriptEditor.Build.cs` 依赖重合的 `ToolMenus`、`ContentBrowserData`、常用 editor subsystem 和菜单上下文。
  - `StaticMeshEditorSubsystem`、`SkeletalMeshEditorSubsystem`、`LevelSequenceEditorSubsystem` 及其他重 editor 依赖类型，必须先经过依赖矩阵复核后再决定是否进入本阶段后半段。
  - `VR Editor`、`MovieScene FBX ControlRig` 导入/导出设置类、明显窄众或重编辑器依赖类型默认继续排除在第一轮之外。
  - editor 绑定测试不能继续全部塞进 `AngelscriptSourceNavigationTests.cpp`；需要在 `AngelscriptTest/Editor/` 下按主题新增测试文件，至少区分 subsystem/menu/context 三类。
- [ ] **P6.1** 📦 Git 提交：`[Binds] Feat: absorb editor subsystem and context bindings`

### Phase 7：收口遗留兼容口、测试与文档

> 目标：在首批迁移落稳后，把遗留的 shard 时代 helper、测试矩阵和文档边界一起收口，避免未来再次出现“源码已经迁了，但结构描述还是旧的”。

- [ ] **P7.1** 收口 legacy helper 与文档描述
  - 当 generated binds 稳定后，重新审视 `AngelscriptBinds.h` 里只为 shard 时代保留的 `BindModuleNames`、`SaveBindModules()`、`LoadBindModules()` 以及相关 skip 表结构，区分“继续保留为兼容能力”和“已无调用可清理”两类。
  - 更新 `AGENTS.md`、`Documents/Guides/Test.md`、相关绑定/迁移文档，明确“参考 shard 只作为迁移输入，不是当前插件最终模块结构”。
  - 这一批文档/结构收口时要显式复核插件边界：绑定相关改动仍应优先落在 `Plugins/Angelscript/`，不把插件逻辑回流到宿主工程模块。
  - 如果迁移过程形成新的依赖矩阵、family 清单或 exclusion list，要把它们沉淀进仓内文档，而不是只留在单次实现记录里。
- [ ] **P7.1** 📦 Git 提交：`[Binds] Docs: document post-shard static bind architecture`

- [ ] **P7.2** 固化阶段验收与回归矩阵
  - 每完成一个 family，至少执行对应模块编译、目标 `Bindings` 测试、必要的 `Editor` 测试；每完成一个大阶段，再跑一次更大的 `Bindings` / `Editor` / `Core.BindConfig` 回归。
  - 对高频类（`AActor`、`USceneComponent`、`UUserWidget`、`Subsystems`、`GameplayAbility` 等）补最小 smoke matrix，防止后续 refactor 时丢失新迁入方法。
  - 对因缺依赖、语义冲突或维护价值不足而暂缓的 family，要把结论写入验收记录，不能默默跳过。
  - 最终 QA 波次至少固定为以下四类命令模板，并要求在执行时按 `Documents/Guides/Build.md` / `Documents/Guides/Test.md` 先从 `AgentConfig.ini` 解析 `EngineRoot` 与项目路径：
    - 编译主目标：使用 `Build.bat` 重新验证宿主 Editor target 与插件编译链，确保新增 runtime/editor 依赖没有破坏基础构建。
    - Engine/BindConfig 基线：运行 `Automation RunTests Angelscript.TestModule.Engine.BindConfig`，确认新 family 的 bind name 仍可被列出、禁用和过滤。
    - Bindings 主回归：运行 `Automation RunTests Angelscript.TestModule.Bindings`，确认迁入的 runtime 绑定至少进入现有 bindings 测试集合。
    - Editor 主回归：运行 `Automation RunTests Angelscript.TestModule.Editor`，其中现有最小锚点是 `Angelscript.TestModule.Editor.SourceNavigation.Functions`，新增 editor bind family 后应补出对应的 `Editor.*` 测试前缀。
- [ ] **P7.2** 📦 Git 提交：`[Binds] Test: lock bind migration regression matrix`

## 验收标准

1. 已形成一份完整的 reference → current 绑定审计表，能直接回答“某个参考 bind 文件当前是否已迁、落在哪、为何延后”。
2. 当前插件的最终迁移方案明确保持 `AngelscriptRuntime` / `AngelscriptEditor` 内部静态注册边界，不把 shard 模块重新引入为长期结构。
3. 首波 runtime / editor 迁移范围、延后范围和默认排除范围都已冻结，并且每类都有明确原因。
4. 每个迁移 family 都绑定到明确的 `Build.cs` 依赖处理策略与测试落点，避免“能编译但不会测”。
5. 遗留的 shard helper、文档描述和测试矩阵都能在后续阶段中被持续收口，而不是永远停留在半迁移状态。

## 风险与注意事项

### 风险 1：把参考 shard 结构误当成当前插件的目标结构

参考插件用 `ASRuntimeBind_xx` / `ASEditorBind_xx` 解决的是历史模块拆分与依赖装配问题；当前插件已经转向插件内静态注册。如果直接恢复 shard 结构，只会把当前仓库重新带回更碎片、更难维护的状态。

### 风险 2：整文件覆盖现有手写绑定，导致脚本语义回退

当前 `Bind_AActor.cpp`、`Bind_UUserWidget.cpp`、`Bind_Subsystems.cpp` 等手写文件已经包含脚本世界上下文、错误提示、类型检查和条件编译处理。若直接用参考文件覆盖，容易丢失这些当前分支已经建立的语义增强。

### 风险 3：过早引入重依赖家族，导致 `Build.cs` 膨胀

`ControlRig`、`RigVM`、`Niagara`、`IKRig`、`Interchange`、`Media*` 等都不是“顺手加一个 include”就结束的类型族。若不先按依赖和测试能力分批，构建和验证成本会迅速失控。

### 风险 4：只迁函数暴露，不补测试和禁用能力

当前仓库已经有 bind name 级别的启用/禁用能力和主题化测试目录。若新批次绑定不进入这些体系，后面很快就会出现“API 已经看见了，但不知道哪些 family 可关闭、哪些调用已经坏了”的维护问题。

### 风险 5：把“有用的绑定全部迁移”理解成“所有参考文件都必须进入当前仓库”

本计划的“有用”必须以当前插件目标、依赖可承接性、维护价值和测试可验证性为准。明显窄众、重编辑器、测试导向或无稳定需求的类型，应该被显式排除，而不是为了追求数量感全部搬进来。
