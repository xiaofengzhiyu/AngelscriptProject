# Angelscript Test Macro System

## Status

This repository previously accumulated documentation for an abandoned
`ANGELSCRIPT_TEST` / `ANGELSCRIPT_ISOLATED_TEST` proposal.

The active test-macro system is now the `ASTEST_*` family implemented in:

- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`
- `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- `Documents/Plans/Plan_TestMacroOptimization.md`

## Current Guidance

### For New Tests

Keep `IMPLEMENT_SIMPLE_AUTOMATION_TEST(...)` for registration, and use the current macro layer inside `RunTest(...)`:

```cpp
#include "Shared/AngelscriptTestMacros.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMyTestName,
	"Angelscript.TestModule.Category.Subcategory.TestName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMyTestName::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	int32 Result = 0;
	ASTEST_COMPILE_RUN_INT(Engine,
		"ASMyTestName",
		TEXT("int Run() { return 42; }"),
		TEXT("int Run()"),
		Result);

	bPassed = TestEqual(TEXT("My test should return the expected value"), Result, 42);

	ASTEST_END_FULL
	return bPassed;
}
```

### Current Macro Families

- Engine creation: `ASTEST_CREATE_ENGINE_FULL()` / `ASTEST_CREATE_ENGINE_SHARE()` / `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` / `ASTEST_CREATE_ENGINE_SHARE_FRESH()` / `ASTEST_CREATE_ENGINE_CLONE()` / `ASTEST_CREATE_ENGINE_NATIVE()`
- Lifecycle: `ASTEST_BEGIN_*` / `ASTEST_END_*`
- Helpers: `ASTEST_COMPILE_RUN_INT`, `ASTEST_COMPILE_RUN_INT64`, `ASTEST_BUILD_MODULE`

## Important Migration Note

Do not infer runtime semantics from older helper names alone.

In `AngelscriptTestUtilities.h`:

- `GetOrCreateSharedCloneEngine()` currently creates a shared Full engine.
- `AcquireCleanSharedCloneEngine()` resets that shared engine before returning it.
- `AcquireFreshSharedCloneEngine()` destroys shared/global state and then reacquires a clean engine.

That means migrating older tests to `ASTEST_*` should be based on actual helper behavior, not just naming similarity with `CLONE`.

## Related Documents

- `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md`
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_MIGRATION_GUIDE.txt`
- `MACRO_VALIDATION_REPORT.md`
- `MACRO_IMPLEMENTATION_SUMMARY.md`

## Historical Note

If you encounter `ANGELSCRIPT_TEST`, `ANGELSCRIPT_ISOLATED_TEST`, or similar names in older notes, treat them as deprecated design artifacts unless and until a compatibility layer is explicitly reintroduced.
