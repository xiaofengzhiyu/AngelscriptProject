# Angelscript Test Macro Implementation Summary

## Status

This file previously summarized an implementation that assumed the `ANGELSCRIPT_*`
wrapper family was complete and validated. That summary is no longer accurate.

The repository is currently converging on the `ASTEST_*` system, and the active
implementation/migration work is tracked in:

- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`
- `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- `Documents/Plans/Plan_TestMacroOptimization.md`

## What Has Actually Landed

1. `AngelscriptTestMacros.h` contains the active two-layer `ASTEST_*` macro system.
2. A first wave of `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/*.cpp` files has already been migrated to `ASTEST_*` patterns.
3. Validation and legacy macro documentation are being reconciled so the repository no longer claims that the abandoned `ANGELSCRIPT_TEST` family is current.

## Current Migration Themes

### 1. Macro Infrastructure

- Keep `ASTEST_CREATE_ENGINE_*()` as the engine-acquisition layer.
- Keep `ASTEST_BEGIN_*` / `ASTEST_END_*` as the lifecycle layer when that pairing is semantically safe.
- Use helper macros only where they remove duplication without obscuring important test intent.

### 2. Semantic Review Before Batch Migration

Older helper names can be misleading:

- `GetOrCreateSharedCloneEngine()` currently creates a shared Full engine.
- `AcquireCleanSharedCloneEngine()` resets that shared engine.
- `AcquireFreshSharedCloneEngine()` tears down shared/global state and reacquires a clean engine.

That means migration decisions must be based on behavior, not on helper naming alone.

### 3. Validation and Documentation Cleanup

- Validation tests should use the active `ASTEST_*` system.
- Legacy docs should either redirect to the current macro guide or be updated to the current terminology.

## Non-Goals

- This repository should no longer claim that the historical `ANGELSCRIPT_TEST` wrapper system is fully validated or production-complete.
- Native / ASSDK / production-engine / performance-sensitive paths should not be bulk-migrated without explicit semantic review.

## References

- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`
- `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- `MACRO_VALIDATION_REPORT.md`
- `Documents/Plans/Plan_TestMacroOptimization.md`
