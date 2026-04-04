#pragma once

#include "AngelscriptTestUtilities.h"
#include "AngelscriptTestEngineHelper.h"
#include "Misc/AutomationTest.h"

namespace AngelscriptTestSupport
{
	#define ANGELSCRIPT_TEST(TestClassName, TestPath, TestFlags) \
		IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClassName, TestPath, TestFlags) \
		bool TestClassName::RunTest(const FString& Parameters)
	
	#define ANGELSCRIPT_ISOLATED_TEST(TestClassName, TestPath, TestFlags) \
		IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClassName, TestPath, TestFlags) \
		bool TestClassName::RunTest(const FString& Parameters) \
		{ \
			TUniquePtr<FAngelscriptEngine> IsolatedEngine = CreateIsolatedCloneEngine(); \
			if (!IsolatedEngine.IsValid()) \
			{ \
				AddError(TEXT("Failed to create isolated Angelscript engine")); \
				return false; \
			} \
			FAngelscriptEngine& Engine = *IsolatedEngine; \
			ON_SCOPE_EXIT \
			{ \
				const TArray<TSharedRef<FAngelscriptModuleDesc>> ActiveModules = Engine.GetActiveModules(); \
				for (const TSharedRef<FAngelscriptModuleDesc>& Module : ActiveModules) \
				{ \
					Engine.DiscardModule(*Module->ModuleName); \
				} \
			};

	#define ANGELSCRIPT_SCOPED_ENGINE(Engine, ScopeName) \
		FAngelscriptEngineScope ScopeName(Engine)

	#define ANGELSCRIPT_ENSURE_ENGINE(EnginePtr, TestObj) \
		if (!(TestObj.TestNotNull(TEXT("Angelscript engine must be valid"), EnginePtr))) \
		{ \
			return false; \
		}

	#define ANGELSCRIPT_REQUIRE_FUNCTION(Module, FunctionDecl, TestObj) \
		GetFunctionByDecl(TestObj, Module, FunctionDecl)

	#define ANGELSCRIPT_EXECUTE_INT(Engine, Function, OutResult, TestObj) \
		ExecuteIntFunction(TestObj, Engine, Function, OutResult)

	#define ANGELSCRIPT_COMPILE_ANNOTATED_MODULE(Engine, ModuleName, Filename, ScriptSource, TestObj) \
		CompileAnnotatedModuleFromMemory(&Engine, FName(ANSI_TO_TCHAR(ModuleName)), FString(ANSI_TO_TCHAR(Filename)), ScriptSource)

	#define ANGELSCRIPT_FIND_GENERATED_CLASS(Engine, ClassName, TestObj) \
		FindGeneratedClass(&Engine, FName(ClassName))

	#define ANGELSCRIPT_EXECUTE_REFLECTED_INT(Object, Function, OutResult, TestObj) \
		ExecuteGeneratedIntEventOnGameThread(Object, Function, OutResult)

	#define ANGELSCRIPT_TEST_VERIFY_COMPILATION_FAILS(Engine, ModuleName, Filename, ScriptSource, TestObj) \
		([](FAngelscriptEngine* _Engine, const TCHAR* _Name, const TCHAR* _File, const FString& _Source) -> bool \
		{ \
			if (_Engine == nullptr) return false; \
			ECompileResult CompileResult = ECompileResult::Error; \
			UE_SET_LOG_VERBOSITY(Angelscript, Fatal); \
			bool bCompiled = CompileModuleWithResult(_Engine, ECompileType::SoftReloadOnly, FName(_Name), FString(_File), _Source, CompileResult); \
			UE_SET_LOG_VERBOSITY(Angelscript, Log); \
			return !bCompiled || (CompileResult == ECompileResult::Error || CompileResult == ECompileResult::ErrorNeedFullReload); \
		})(&Engine, ANSI_TO_TCHAR(ModuleName), ANSI_TO_TCHAR(Filename), ScriptSource)

	#define ANGELSCRIPT_COMPILE_WITH_TRACE(Engine, ModuleName, Filename, ScriptSource, OutTrace, TestObj) \
		CompileModuleWithSummary(&Engine, ECompileType::Initial, FName(ANSI_TO_TCHAR(ModuleName)), FString(ANSI_TO_TCHAR(Filename)), ScriptSource, false, OutTrace, false)

	#define ANGELSCRIPT_MULTI_PHASE_COMPILE(Engine, ModuleName, Filename, Phase1Source, Phase2Source, OutPhase1Result, OutPhase2Result, TestObj) \
		do \
		{ \
			CompileModuleWithResult(&Engine, ECompileType::FullReload, FName(ANSI_TO_TCHAR(ModuleName)), FString(ANSI_TO_TCHAR(Filename)), Phase1Source, OutPhase1Result); \
			CompileModuleWithResult(&Engine, ECompileType::SoftReloadOnly, FName(ANSI_TO_TCHAR(ModuleName)), FString(ANSI_TO_TCHAR(Filename)), Phase2Source, OutPhase2Result); \
		} while (false)
}

#endif
