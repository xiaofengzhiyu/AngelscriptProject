#include "../../Shared/AngelscriptLearningTrace.h"
#include "../../Shared/AngelscriptTestEngineHelper.h"
#include "../../Shared/AngelscriptTestUtilities.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Preprocessor/AngelscriptPreprocessor.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace
{
	struct FLearningCompilerScenario
	{
		FString ApiLabel;
		FName ModuleName;
		FString Filename;
		FString Script;
		ECompileType CompileType = ECompileType::SoftReloadOnly;
		bool bUsesPreprocessor = false;
		bool bSuppressCompileErrorLogs = false;
	};

	struct FLearningCompilerOutcome
	{
		bool bCompileSucceeded = false;
		ECompileResult CompileResult = ECompileResult::Error;
		int32 ModuleDescCount = 0;
		int32 CompiledModuleCount = 0;
		TArray<FString> ModuleNames;
		TArray<FString> AbsoluteFilenames;
		TArray<FAngelscriptLearningTraceDiagnostic> Diagnostics;
	};

	FString GetCompileTypeLabel(ECompileType CompileType)
	{
		switch (CompileType)
		{
		case ECompileType::Initial:
			return TEXT("Initial");
		case ECompileType::SoftReloadOnly:
			return TEXT("SoftReloadOnly");
		case ECompileType::FullReload:
			return TEXT("FullReload");
		default:
			return TEXT("Unknown");
		}
	}

	FString GetCompileResultLabel(ECompileResult CompileResult)
	{
		switch (CompileResult)
		{
		case ECompileResult::Error:
			return TEXT("Error");
		case ECompileResult::ErrorNeedFullReload:
			return TEXT("ErrorNeedFullReload");
		case ECompileResult::PartiallyHandled:
			return TEXT("PartiallyHandled");
		case ECompileResult::FullyHandled:
			return TEXT("FullyHandled");
		default:
			return TEXT("Unknown");
		}
	}

	FString GetDiagnosticSeverityLabel(const FAngelscriptEngine::FDiagnostic& Diagnostic)
	{
		if (Diagnostic.bIsError)
		{
			return TEXT("Error");
		}

		if (Diagnostic.bIsInfo)
		{
			return TEXT("Info");
		}

		return TEXT("Warning");
	}

	void AddUniqueAbsoluteFilename(TArray<FString>& AbsoluteFilenames, const FString& AbsoluteFilename)
	{
		if (!AbsoluteFilename.IsEmpty())
		{
			AbsoluteFilenames.AddUnique(AbsoluteFilename);
		}
	}

	TSharedRef<FAngelscriptModuleDesc> MakeLearningModuleDesc(const FLearningCompilerScenario& Scenario)
	{
		const FString RelativeFilename = FPaths::Combine(TEXT("Automation"), Scenario.Filename);
		const FString AbsoluteFilename = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), Scenario.Filename);

		TSharedRef<FAngelscriptModuleDesc> ModuleDesc = MakeShared<FAngelscriptModuleDesc>();
		ModuleDesc->ModuleName = Scenario.ModuleName.ToString();

		FAngelscriptModuleDesc::FCodeSection& Section = ModuleDesc->Code.AddDefaulted_GetRef();
		Section.RelativeFilename = RelativeFilename;
		Section.AbsoluteFilename = AbsoluteFilename;
		Section.Code = Scenario.Script;
		Section.CodeHash = static_cast<int64>(FCrc::StrCrc32(*Section.Code));
		ModuleDesc->CodeHash ^= Section.CodeHash;
		return ModuleDesc;
	}

	bool BuildModulesForScenario(const FLearningCompilerScenario& Scenario, TArray<TSharedRef<FAngelscriptModuleDesc>>& OutModules, TArray<FString>& OutAbsoluteFilenames)
	{
		if (Scenario.bUsesPreprocessor)
		{
			const FString AutomationDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"));
			const FString AbsoluteFilename = FPaths::Combine(AutomationDirectory, Scenario.Filename);
			IFileManager::Get().MakeDirectory(*AutomationDirectory, true);
			if (!FFileHelper::SaveStringToFile(Scenario.Script, *AbsoluteFilename))
			{
				return false;
			}

			FAngelscriptPreprocessor Preprocessor;
			Preprocessor.AddFile(Scenario.Filename, AbsoluteFilename);
			if (!Preprocessor.Preprocess())
			{
				return false;
			}

			OutModules = Preprocessor.GetModulesToCompile();
			for (const TSharedRef<FAngelscriptModuleDesc>& Module : OutModules)
			{
				for (const FAngelscriptModuleDesc::FCodeSection& Section : Module->Code)
				{
					AddUniqueAbsoluteFilename(OutAbsoluteFilenames, Section.AbsoluteFilename);
				}
			}
			return OutModules.Num() > 0;
		}

		TSharedRef<FAngelscriptModuleDesc> ModuleDesc = MakeLearningModuleDesc(Scenario);
		for (const FAngelscriptModuleDesc::FCodeSection& Section : ModuleDesc->Code)
		{
			AddUniqueAbsoluteFilename(OutAbsoluteFilenames, Section.AbsoluteFilename);
		}
		OutModules.Add(ModuleDesc);
		return true;
	}

	void CollectDiagnosticsForScenario(FAngelscriptEngine& Engine, const TArray<FString>& AbsoluteFilenames, TArray<FAngelscriptLearningTraceDiagnostic>& OutDiagnostics)
	{
		for (const FString& AbsoluteFilename : AbsoluteFilenames)
		{
			if (const FAngelscriptEngine::FDiagnostics* Diagnostics = Engine.Diagnostics.Find(AbsoluteFilename))
			{
				for (const FAngelscriptEngine::FDiagnostic& Diagnostic : Diagnostics->Diagnostics)
				{
					FAngelscriptLearningTraceDiagnostic& TraceDiagnostic = OutDiagnostics.AddDefaulted_GetRef();
					TraceDiagnostic.Section = AbsoluteFilename;
					TraceDiagnostic.Row = Diagnostic.Row;
					TraceDiagnostic.Column = Diagnostic.Column;
					TraceDiagnostic.Severity = GetDiagnosticSeverityLabel(Diagnostic);
					TraceDiagnostic.Message = Diagnostic.Message;
				}
			}
		}
	}

	FLearningCompilerOutcome RunCompilerScenario(FAngelscriptEngine& Engine, const FLearningCompilerScenario& Scenario)
	{
		FLearningCompilerOutcome Outcome;
		Engine.Diagnostics.Empty();
		Engine.LastEmittedDiagnostics.Empty();
		Engine.bDiagnosticsDirty = false;

		TArray<TSharedRef<FAngelscriptModuleDesc>> ModulesToCompile;
		if (!BuildModulesForScenario(Scenario, ModulesToCompile, Outcome.AbsoluteFilenames))
		{
			Outcome.CompileResult = ECompileResult::Error;
			CollectDiagnosticsForScenario(Engine, Outcome.AbsoluteFilenames, Outcome.Diagnostics);
			return Outcome;
		}

		Outcome.ModuleDescCount = ModulesToCompile.Num();
		for (const TSharedRef<FAngelscriptModuleDesc>& Module : ModulesToCompile)
		{
			Outcome.ModuleNames.Add(Module->ModuleName);
		}

		TArray<TSharedRef<FAngelscriptModuleDesc>> CompiledModules;
		TGuardValue<bool> AutomaticImportGuard(FAngelscriptEngine::bUseAutomaticImportMethod, false);
		FScopedAutomaticImportsOverride AutomaticImportsOverride(Engine.GetScriptEngine());
		FScopedTestEngineGlobalScope GlobalScope(&Engine);
		if (Scenario.bSuppressCompileErrorLogs)
		{
			UE_SET_LOG_VERBOSITY(Angelscript, Fatal);
		}
		Outcome.CompileResult = Engine.CompileModules(Scenario.CompileType, ModulesToCompile, CompiledModules);
		if (Scenario.bSuppressCompileErrorLogs)
		{
			UE_SET_LOG_VERBOSITY(Angelscript, Log);
		}
		Outcome.CompiledModuleCount = CompiledModules.Num();
		Outcome.bCompileSucceeded = Outcome.CompileResult == ECompileResult::FullyHandled || Outcome.CompileResult == ECompileResult::PartiallyHandled;

		CollectDiagnosticsForScenario(Engine, Outcome.AbsoluteFilenames, Outcome.Diagnostics);
		return Outcome;
	}

	void TraceCompilerScenario(FAngelscriptLearningTraceSession& Trace, const FLearningCompilerScenario& Scenario, const FLearningCompilerOutcome& Outcome)
	{
		Trace.AddStep(Scenario.ApiLabel, Outcome.bCompileSucceeded ? TEXT("Compilation path completed without a hard error") : TEXT("Compilation path reported an error or missing preprocessing result"));
		Trace.AddKeyValue(TEXT("ScriptFilename"), Scenario.Filename);
		Trace.AddKeyValue(TEXT("CompileType"), GetCompileTypeLabel(Scenario.CompileType));
		Trace.AddKeyValue(TEXT("UsesPreprocessor"), Scenario.bUsesPreprocessor ? TEXT("true") : TEXT("false"));
		Trace.AddKeyValue(TEXT("ModuleDescCount"), FString::FromInt(Outcome.ModuleDescCount));
		Trace.AddKeyValue(TEXT("CompiledModuleCount"), FString::FromInt(Outcome.CompiledModuleCount));
		Trace.AddKeyValue(TEXT("CompileResult"), GetCompileResultLabel(Outcome.CompileResult));
		Trace.AddKeyValue(TEXT("DiagnosticsCount"), FString::FromInt(Outcome.Diagnostics.Num()));

		if (Outcome.ModuleNames.Num() > 0)
		{
			Trace.AddCodeBlock(FormatLearningTraceStringList(Outcome.ModuleNames, TEXT("ModuleNames")));
		}

		if (Outcome.Diagnostics.Num() > 0)
		{
			Trace.AddCodeBlock(FormatLearningTraceDiagnostics(Outcome.Diagnostics));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptLearningCompilerTraceTest,
	"Angelscript.TestModule.Learning.Runtime.CompilerPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptLearningCompilerTraceTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = GetSharedTestEngine();
	ResetSharedTestEngine(Engine);
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("LearningCompilerBuildModule"));
		Engine.DiscardModule(TEXT("LearningCompilerPlainModule"));
		Engine.DiscardModule(TEXT("LearningCompilerAnnotatedModule"));
		Engine.DiscardModule(TEXT("LearningCompilerBrokenAnnotatedModule"));
	};

	FAngelscriptLearningTraceSinkConfig SinkConfig;
	SinkConfig.bEmitToAutomation = true;
	SinkConfig.bEmitToLog = true;
	SinkConfig.bEmitToFile = false;

	FAngelscriptLearningTraceSession Trace(TEXT("LearningCompilerPipeline"), SinkConfig);
	Trace.BeginPhase(EAngelscriptLearningTracePhase::Compile);

	const FLearningCompilerScenario BuildModuleScenario{
		TEXT("BuildModuleStyle.InitialCompile"),
		TEXT("LearningCompilerBuildModule"),
		TEXT("LearningCompilerBuildModule.as"),
		TEXT("int Entry() { return 42; }"),
		ECompileType::Initial,
		true,
	};
	const FLearningCompilerOutcome BuildModuleOutcome = RunCompilerScenario(Engine, BuildModuleScenario);
	TraceCompilerScenario(Trace, BuildModuleScenario, BuildModuleOutcome);

	const FLearningCompilerScenario PlainCompileScenario{
		TEXT("CompileModuleFromMemoryStyle.SoftReloadOnly"),
		TEXT("LearningCompilerPlainModule"),
		TEXT("LearningCompilerPlainModule.as"),
		TEXT("int Entry() { int Base = 40; return Base + 2; }"),
		ECompileType::SoftReloadOnly,
		false,
	};
	const FLearningCompilerOutcome PlainCompileOutcome = RunCompilerScenario(Engine, PlainCompileScenario);
	TraceCompilerScenario(Trace, PlainCompileScenario, PlainCompileOutcome);

	const FLearningCompilerScenario AnnotatedCompileScenario{
		TEXT("CompileAnnotatedModuleFromMemoryStyle.FullReload"),
		TEXT("LearningCompilerAnnotatedModule"),
		TEXT("LearningCompilerAnnotatedModule.as"),
		TEXT(R"(
UCLASS()
class ULearningCompilerTraceCarrier : UObject
{
	UFUNCTION()
	int GetValue()
	{
		return 42;
	}
}
)") ,
		ECompileType::FullReload,
		true,
	};
	const FLearningCompilerOutcome AnnotatedCompileOutcome = RunCompilerScenario(Engine, AnnotatedCompileScenario);
	TraceCompilerScenario(Trace, AnnotatedCompileScenario, AnnotatedCompileOutcome);

	const FLearningCompilerScenario BrokenAnnotatedScenario{
		TEXT("CompileAnnotatedModuleFromMemoryStyle.Diagnostics"),
		TEXT("LearningCompilerBrokenAnnotatedModule"),
		TEXT("LearningCompilerBrokenAnnotatedModule.as"),
		TEXT(R"(
UCLASS()
class UBrokenLearningCompilerTraceCarrier : UObject
{
	UFUNCTION()
	MissingType GetValue()
	{
		MissingType Value;
		return Value;
	}
}
)") ,
		ECompileType::FullReload,
		true,
		true,
	};
	const FLearningCompilerOutcome BrokenAnnotatedOutcome = RunCompilerScenario(Engine, BrokenAnnotatedScenario);
	TraceCompilerScenario(Trace, BrokenAnnotatedScenario, BrokenAnnotatedOutcome);

	UClass* GeneratedClass = FindGeneratedClass(&Engine, TEXT("ULearningCompilerTraceCarrier"));
	if (GeneratedClass != nullptr)
	{
		Trace.AddStep(TEXT("GeneratedClassVisibility"), TEXT("Annotated compiler path produced a generated Unreal class that can be discovered after compilation"));
		Trace.AddKeyValue(TEXT("GeneratedClass"), GeneratedClass->GetName());
	}
	else
	{
		Trace.AddStep(TEXT("GeneratedClassVisibility"), TEXT("Annotated compiler path did not leave a discoverable generated Unreal class"));
		Trace.AddKeyValue(TEXT("GeneratedClass"), TEXT("<null>"));
	}

	const bool bBuildModuleSucceeded = TestTrue(TEXT("BuildModule-style initial compile should succeed"), BuildModuleOutcome.bCompileSucceeded);
	const bool bPlainCompileSucceeded = TestTrue(TEXT("CompileModuleFromMemory-style compile should succeed"), PlainCompileOutcome.bCompileSucceeded);
	const bool bAnnotatedCompileSucceeded = TestTrue(TEXT("CompileAnnotatedModuleFromMemory-style compile should succeed"), AnnotatedCompileOutcome.bCompileSucceeded);
	const bool bBrokenCompileFailed = TestFalse(TEXT("Broken annotated compiler input should fail"), BrokenAnnotatedOutcome.bCompileSucceeded);
	const bool bBrokenDiagnosticsPresent = TestTrue(TEXT("Broken annotated compiler input should produce diagnostics"), BrokenAnnotatedOutcome.Diagnostics.Num() > 0);
	const bool bGeneratedClassVisible = TestNotNull(TEXT("Annotated compiler path should leave a generated class visible for later runtime tests"), GeneratedClass);
	const bool bPhaseSequenceOk = AssertLearningTracePhaseSequence(*this, Trace.GetEvents(), {
		EAngelscriptLearningTracePhase::Compile,
	});
	const bool bContainsCompileResultKeyword = AssertLearningTraceContainsKeyword(*this, Trace.GetEvents(), TEXT("CompileResult"));
	const bool bContainsDiagnosticsKeyword = AssertLearningTraceContainsKeyword(*this, Trace.GetEvents(), TEXT("DiagnosticsCount"));
	const bool bMinimumEventsOk = AssertLearningTraceMinimumEventCount(*this, Trace.GetEvents(), 5);

	Trace.FlushToAutomation(*this);
	Trace.FlushToLog();
	return bBuildModuleSucceeded
		&& bPlainCompileSucceeded
		&& bAnnotatedCompileSucceeded
		&& bBrokenCompileFailed
		&& bBrokenDiagnosticsPresent
		&& bGeneratedClassVisible
		&& bPhaseSequenceOk
		&& bContainsCompileResultKeyword
		&& bContainsDiagnosticsKeyword
		&& bMinimumEventsOk;
}

#endif
