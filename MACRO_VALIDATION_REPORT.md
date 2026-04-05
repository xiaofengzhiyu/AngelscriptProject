# Angelscript Test Macro Validation Report

## Status Snapshot

This file previously reported validation progress for an abandoned
`ANGELSCRIPT_*` wrapper family. That report is now obsolete.

The active validation target is the `ASTEST_*` system implemented in:

- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`
- `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptCompilerMacroValidationTests.cpp`

## What Validation Means Now

Current validation work should prove all of the following:

1. Validation tests use the active `ASTEST_*` API instead of historical `ANGELSCRIPT_TEST` names.
2. Macro-assisted tests still preserve the underlying test intent, Automation paths, and assertions.
3. Engine-acquisition semantics are not changed accidentally during migration, especially where older helpers imply reset or fresh-engine behavior.
4. Build and automation-test verification use the repository-standard entry points:
   - `Tools/RunBuild.ps1`
   - `Tools/RunTests.ps1`
   - `Tools/RunTestSuite.ps1`

## Current Validation Scope

### Validation Files

- `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`
- `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptCompilerMacroValidationTests.cpp`

### Canonical Macro Files

- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`
- `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`

### Related Migration Plan

- `Documents/Plans/Plan_TestMacroOptimization.md`

## Historical Note

Any references in older reports to:

- `ANGELSCRIPT_TEST`
- `ANGELSCRIPT_ISOLATED_TEST`
- “12 implemented macros” in the abandoned wrapper family

should be interpreted as historical design exploration, not as the current validated macro API.

## Next Verification Steps

After the active `ASTEST_*` migration changes land, validate using the repository-standard commands:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -TimeoutMs 180000 -- -NoXGE
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Validation." -TimeoutMs 300000
```

If broader directories are migrated, extend verification to the relevant prefixes or groups documented in `Plan_TestMacroOptimization.md`.
