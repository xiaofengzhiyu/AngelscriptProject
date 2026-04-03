# 脚本自定义结构体实现机制

> 说明我们的插件如何在 **不修改引擎** 的前提下支持 Angelscript 定义 USTRUCT，并与 Hazelight 的引擎修改方案做对比。

## 1. 问题域

当脚本定义一个 `struct` 时，UE 引擎需要知道如何对这块内存执行以下操作：

| 操作 | UE 内部入口 | 触发场景 |
|------|------------|---------|
| Construct | `ICppStructOps::Construct` | 属性初始化、CDO 构造、数组扩容 |
| Destruct | `ICppStructOps::Destruct` | 对象销毁、属性重置 |
| Copy | `ICppStructOps::Copy` | 属性复制、网络复制、序列化 |
| Identical | `ICppStructOps::Identical` | Diff 比较、属性面板变更检测 |
| GetTypeHash | `ICppStructOps::GetStructTypeHash` | TMap/TSet 键、属性哈希 |

对于原生 C++ 结构体，这些操作在编译期由 `TCppStructOps<T>` 模板自动生成。但脚本结构体是运行时创建的，无法走这条路。

## 2. UE5 的 FakeVTable 机制

UE5 的 `ICppStructOps` **不使用 C++ 虚函数**，而是采用了自定义的"伪虚表"（FakeVTable）机制：

```
ICppStructOps（基类，非多态）
  ├─ Size, Alignment       // 结构体大小和对齐
  └─ FakeVPtr ──────────→ FStructOpsFakeVTable
                             ├─ Flags          // 位域：哪些操作可用
                             ├─ Capabilities    // 能力描述
                             └─ 函数指针数组[]   // 按 Flag 位序排列
```

**调用流程**（引擎侧）：

```cpp
// Class.h 中的 FStructOpsFakeVTable::GetHandlingFunc<Flag>()
// 1. 检查 Flags 中该操作位是否设置
// 2. 用 popcount 算出该位在函数指针数组中的索引
// 3. 返回对应的函数指针
auto* ConstructFunc = FakeVTable->GetHandlingFunc<Construct>();
ConstructFunc(StructOps, DestMemory);
//            ^^^^^^^^^ 第一个参数是 ICppStructOps* 本身
```

关键细节：**函数指针的第一个参数是 `ICppStructOps*` 自身**。这为运行时注入提供了基础——只要我们的子结构体继承 `ICppStructOps`，就能通过 `this` 指针访问自定义数据。

## 3. 我们的实现

### 3.1 类型层次

```
UScriptStruct（UE 原生）
  └─ UASStruct（我们的子类）
       ├─ ScriptType: asITypeInfo*     // 指向 AngelScript 类型
       ├─ NewerVersion: UASStruct*     // 热重载链
       ├─ Guid: FGuid                  // 序列化稳定标识
       └─ CppStructOps → FASStructOps（我们的 ICppStructOps 子结构体）
```

### 3.2 FASStructOps 核心结构

`ASStruct.cpp` 中的 `FASStructOps` 是整个机制的核心：

```cpp
struct FASStructOps : UASStruct::ICppStructOps
{
    // ---- 脚本侧数据 ----
    UASStruct* Struct;
    asCObjectType* ScriptType;

    asIScriptFunction* ConstructFunction;  // 脚本构造函数
    asIScriptFunction* EqualsFunction;     // 脚本 opEquals 方法
    asIScriptFunction* ToStrFunction;      // 脚本 ToString 方法
    asIScriptFunction* HashFunction;       // 脚本 Hash 方法

    // ---- 伪虚表 ----
    struct FASFakeVTable : public FStructOpsFakeVTable
    {
        void* Construct;
        void* Destruct;
        void* Copy;
        void* Identical;
        void* GetStructTypeHash;
    };
    FASFakeVTable FakeVTable;
};
```

### 3.3 构造时的 FakeVTable 注入

```cpp
FASStructOps(UASStruct* InStruct, int32 InSize, int32 InAlignment)
    : ICppStructOps(InSize, InAlignment)
{
    // 1. 把 FakeVPtr 指向我们自定义的表
    FakeVPtr = &FakeVTable;

    // 2. 设置 Flags，告诉引擎我们支持哪些操作
    FakeVTable.Flags = Construct | Destruct | Copy | Identical | GetStructTypeHash;

    // 3. 填入静态函数指针
    FakeVTable.Construct       = &FASStructOps::Construct;
    FakeVTable.Destruct        = &FASStructOps::Destruct;
    FakeVTable.Copy            = &FASStructOps::Copy;
    FakeVTable.Identical       = &FASStructOps::Identical;
    FakeVTable.GetStructTypeHash = &FASStructOps::GetStructTypeHash;

    // 4. 设置能力标志
    Capabilities.HasDestructor  = true;
    Capabilities.HasCopy        = true;
    Capabilities.HasIdentical   = (EqualsFunction != nullptr);
    Capabilities.HasGetTypeHash = (HashFunction != nullptr);
}
```

### 3.4 操作函数实现

每个静态函数的第一个参数都是 `FASStructOps*`（由引擎传入 `ICppStructOps* this` 后自动转型），从中获取 `ScriptType` 和脚本函数指针：

| 操作 | 实现逻辑 |
|------|---------|
| `Construct(Ops, Dest)` | 有脚本构造函数 → 调用 AS VM 执行；否则 → `Memzero` |
| `Destruct(Ops, Dest)` | 调用 `asCScriptObject::CallDestructor(ScriptType)` |
| `Copy(Ops, Dest, Src)` | 调用 `asCScriptObject::PerformCopy(Source, Type, Type)` |
| `Identical(Ops, A, B)` | 有 `opEquals` → 调用 AS VM 执行并返回结果；否则 → 返回 false |
| `GetStructTypeHash(Ops, Src)` | 有 `Hash()` → 调用 AS VM 执行并返回结果；否则 → 返回 0 |

### 3.5 脚本函数发现

`SetFromStruct()` 在创建和热重载时被调用，通过 AngelScript 类型信息自动发现脚本方法：

| 脚本方法 | 发现方式 | 要求的签名 |
|---------|---------|-----------|
| 构造函数 | `ScriptType->beh.construct` | 默认构造函数 |
| `opEquals` | `ScriptType->GetFirstMethod("opEquals")` | `bool opEquals(const T& Other) const` |
| `ToString` | `ScriptType->GetFirstMethod("ToString")` | `FString ToString() const` |
| `Hash` | `ScriptType->GetFirstMethod("Hash")` | `uint32 Hash() const` |

## 4. 完整生命周期

### 4.1 创建阶段（编译时）

`AngelscriptClassGenerator::CreateFullReloadStruct()`:

```
1. NewObject<UASStruct>(Package, Name)     // 创建 UE 反射结构体对象
2. bIsScriptStruct = true                  // 标记为脚本结构体
3. SetPropertiesSize(ScriptType->GetSize())// 大小 = AngelScript 对象大小
4. SetGuid(Name)                           // 生成序列化稳定 GUID
5. NotifyRegistrationEvent(NRT_Struct)     // 通知 UE 加载系统
```

### 4.2 初始化阶段（链接时）

`AngelscriptClassGenerator::DoFullReloadStruct()`:

```
1. Bind()                                  // 绑定结构体
2. SetCppStructOps(CreateCppStructOps())   // 创建 FASStructOps + FakeVTable
3. PrepareCppStructOps()                   // 注册到 UE 系统
4. AddClassProperties()                    // 把脚本属性映射为 FProperty
5. StaticLink()                            // 完成属性链接
```

### 4.3 运行时调用

```
UE 需要构造/复制/比较结构体
  → ICppStructOps* Ops = Struct->GetCppStructOps()
  → FakeVPtr->GetHandlingFunc<操作>()
  → FASStructOps::操作函数(Ops, ...)
  → AngelScript VM 执行脚本方法
```

### 4.4 热重载

```
UASStruct::UpdateScriptType()
  → FASStructOps::SetFromStruct()          // 重新发现脚本方法
  → 更新 StructFlags (STRUCT_IdenticalNative)
  → 旧版本通过 NewerVersion 链指向新版本
```

## 5. 与 Hazelight 方案的对比

| 维度 | Hazelight（UEAS2） | 我们（AngelPortV2） |
|------|-------------------|-------------------|
| **方法** | 修改 `ICppStructOps` 接口签名，给每个方法加 `void* StructOps` 参数 | 利用 UE5 原生 FakeVTable 机制，手动构造伪虚表 |
| **引擎修改** | 需要修改 `Class.h` ~24 处 | **不需要任何引擎修改** |
| **ABI 兼容性** | 破坏——所有使用 `ICppStructOps` 的代码必须重新编译 | 完全兼容——不改变任何 ABI |
| **功能完整性** | Construct / Destruct / Copy / Identical / ExportTextItem | Construct / Destruct / Copy / Identical / GetStructTypeHash / ToString |
| **脚本方法发现** | 通过 `void* StructOps` 参数传入脚本类型 | 通过 `FASStructOps*` 的 `this` 指针访问脚本类型（等价） |
| **热重载支持** | 不明确 | `UpdateScriptType()` + `NewerVersion` 链 |
| **升级风险** | 引擎升级时需要同步修改 | 依赖 `FStructOpsFakeVTable` 布局稳定（UE5 内部 API） |

### 关键结论

Hazelight 修改 `ICppStructOps` 接口签名是 **不必要的**。UE5 的 FakeVTable 机制已经天然支持运行时注入自定义结构体操作：

1. `ICppStructOps` 不是多态类，`FakeVPtr` 是普通成员指针
2. 函数指针调用时传入 `ICppStructOps*` 作为第一个参数
3. 我们的 `FASStructOps` 继承 `ICppStructOps`，通过 `this` 就能访问所有脚本侧数据

这种方式比 Hazelight 的方案更干净——零引擎修改，零 ABI 破坏，且功能等价。
