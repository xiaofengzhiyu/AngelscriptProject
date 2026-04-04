# Angelscript Test Macros

## Quick Start

Add this include to your test file:
```cpp
#include "Shared/AngelscriptTestMacros.h"
```

Then use the appropriate macro for your test type:

### Simple Shared Engine Test
```cpp
ANGELSCRIPT_TEST(FMySimpleTest, "Angelscript.Category.Test", EditorContext | EngineFilter)
{
    FAngelscriptEngine& Engine = GetOrCreateSharedCloneEngine();
    asIScriptModule* Module = BuildModule(*this, Engine, "TestModule", TestScript);
    if (!Module) return false;
    
    // Test body...
    return true;
}
```

### Isolated Engine Test  
```cpp
ANGELSCRIPT_ISOLATED_TEST(FMyIsolatedTest, "Angelscript.Category.Isolated", EditorContext)
{
    // Engine is automatically created and cleaned up
    asIScriptModule* Module = BuildModule(*this, Engine, "TestModule", TestScript);
    if (!Module) return false;
    
    // Test body...
    return true;
}
```

## Macros Reference

### Test Declaration
- `ANGELSCRIPT_TEST` - Shared engine test wrapper
- `ANGELSCRIPT_ISOLATED_TEST` - Isolated engine test wrapper with auto-cleanup

### Engine Utilities
- `ANGELSCRIPT_SCOPED_ENGINE` - Create engine context scope
- `ANGELSCRIPT_ENSURE_ENGINE` - Validate engine is valid

### Function Execution
- `ANGELSCRIPT_REQUIRE_FUNCTION` - Get function with error reporting
- `ANGELSCRIPT_EXECUTE_INT` - Execute int-returning function with null checks
- `ANGELSCRIPT_EXECUTE_REFLECTED_INT` - Execute reflected function on game thread

### Compilation
- `ANGELSCRIPT_COMPILE_ANNOTATED_MODULE` - Compile with preprocessor
- `ANGELSCRIPT_COMPILE_WITH_TRACE` - Compile with diagnostics
- `ANGELSCRIPT_TEST_VERIFY_COMPILATION_FAILS` - Negative compilation test
- `ANGELSCRIPT_MULTI_PHASE_COMPILE` - Two-phase compilation (hot reload)

### Class Lookup
- `ANGELSCRIPT_FIND_GENERATED_CLASS` - Find generated UClass with error reporting

## Impact

**Boilerplate Reduction: 60-90% depending on test complexity**

- Simple tests: ~6-8 lines saved
- Isolated tests: ~12-15 lines saved
- Function execution: ~8-10 lines per call
- Compilation tests: ~14-20 lines saved

## Documentation

- `MACRO_ANALYSIS.txt` - Detailed before/after analysis with exact line counts
- `MACRO_MIGRATION_GUIDE.txt` - Step-by-step migration instructions

## Design Principles

1. **Error Propagation** - Early returns reduce nesting
2. **Automatic Reporting** - Errors logged via TestObj
3. **Type Safety** - Compile-time checking via lambdas
4. **Zero Overhead** - Compile-time macro expansion only
5. **Backward Compatible** - Works alongside existing tests

## Examples

### Execution with Validation
```cpp
int32 Result = 0;
asIScriptFunction* Func = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
if (!ANGELSCRIPT_EXECUTE_INT(Engine, Func, Result, *this)) return false;

TestEqual(TEXT("Result"), Result, 42);
```

### Annotated Module Compilation
```cpp
bool bCompiled = ANGELSCRIPT_COMPILE_ANNOTATED_MODULE(
    Engine, "TestModule", "test.as", AnnotatedScript, *this
);
if (!bCompiled) return false;
```

### Multi-Phase Compilation
```cpp
ECompileResult Phase1Result, Phase2Result;
ANGELSCRIPT_MULTI_PHASE_COMPILE(
    Engine, "Module", "module.as", Script1, Script2, 
    Phase1Result, Phase2Result, *this
);
```

### Class Lookup and Instantiation
```cpp
UClass* ActorClass = ANGELSCRIPT_FIND_GENERATED_CLASS(Engine, TEXT("AMyActor"), *this);
if (!ActorClass) return false;

AActor* Instance = NewObject<AActor>(GetTransientPackage(), ActorClass);
```

## Notes

- All macros are in the `AngelscriptTestSupport` namespace
- Most helper macros are optional (can use raw functions too)
- Test declaration macros (ANGELSCRIPT_TEST, ANGELSCRIPT_ISOLATED_TEST) provide the most impact
- Macros use lambdas internally for type safety
- Zero runtime overhead (all compile-time)

## Migration

See `MACRO_MIGRATION_GUIDE.txt` for phased migration instructions.

