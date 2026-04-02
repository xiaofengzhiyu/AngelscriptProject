#include "Shared/AngelscriptLearningTrace.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptLearningTraceSequenceTest,
	"Angelscript.TestModule.Shared.LearningTrace.Sequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptLearningTraceFileExportTest,
	"Angelscript.TestModule.Shared.LearningTrace.FileExport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptLearningTraceSequenceTest::RunTest(const FString& Parameters)
{
	FAngelscriptLearningTraceSinkConfig SinkConfig;
	SinkConfig.bEmitToAutomation = false;
	SinkConfig.bEmitToLog = false;
	SinkConfig.bEmitToFile = false;

	FAngelscriptLearningTraceSession Trace(TEXT("LearningTraceSequence"), SinkConfig);
	Trace.BeginPhase(EAngelscriptLearningTracePhase::EngineBootstrap);
	Trace.AddStep(TEXT("CreateEngine"), TEXT("Created a native script engine instance"));
	Trace.AddKeyValue(TEXT("ReturnCode"), TEXT("0"));
	Trace.BeginPhase(EAngelscriptLearningTracePhase::Compile);
	Trace.AddStep(TEXT("BuildModule"), TEXT("Built a module from in-memory source"));

	const TArray<FAngelscriptLearningTraceEvent>& Events = Trace.GetEvents();
	if (!TestEqual(TEXT("Trace session should capture two events"), Events.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("The first event should use the current phase as its step prefix"), Events[0].StepId, FString(TEXT("EngineBootstrap.01")));
	TestEqual(TEXT("The second phase should reset numbering within its own prefix"), Events[1].StepId, FString(TEXT("Compile.01")));
	TestEqual(TEXT("The first event should capture one evidence key/value pair"), Events[0].Evidence.Num(), 1);

	const TArray<FString> SummaryLines = Trace.BuildSummaryLines();
	if (!TestEqual(TEXT("Summary lines should mirror the number of events"), SummaryLines.Num(), 2))
	{
		return false;
	}

	TestTrue(TEXT("Summary output should mention the generated step id"), SummaryLines[0].Contains(TEXT("EngineBootstrap.01")));
	TestTrue(TEXT("Summary output should mention the recorded action"), SummaryLines[0].Contains(TEXT("CreateEngine")));
	return true;
}

bool FAngelscriptLearningTraceFileExportTest::RunTest(const FString& Parameters)
{
	const FString OutputRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/AngelscriptLearningTraceTests"));
	IFileManager::Get().DeleteDirectory(*OutputRoot, false, true);

	FAngelscriptLearningTraceSinkConfig SinkConfig;
	SinkConfig.bEmitToAutomation = false;
	SinkConfig.bEmitToLog = false;
	SinkConfig.bEmitToFile = true;
	SinkConfig.OutputRoot = OutputRoot;

	FAngelscriptLearningTraceSession Trace(TEXT("LearningTraceFileExport"), SinkConfig);
	Trace.BeginPhase(EAngelscriptLearningTracePhase::Execution);
	Trace.AddStep(TEXT("ExecuteFunction"), TEXT("Executed a function and captured the result"));
	Trace.AddKeyValue(TEXT("Result"), TEXT("42"));

	const bool bFlushed = Trace.FlushToFile();
	if (!TestTrue(TEXT("FlushToFile should create a trace artifact"), bFlushed))
	{
		return false;
	}

	const FString ExpectedFile = FPaths::Combine(OutputRoot, TEXT("LearningTraceFileExport.trace.txt"));
	if (!TestTrue(TEXT("Trace file should exist after FlushToFile"), IFileManager::Get().FileExists(*ExpectedFile)))
	{
		return false;
	}

	FString SavedText;
	if (!FFileHelper::LoadFileToString(SavedText, *ExpectedFile))
	{
		AddError(FString::Printf(TEXT("Expected trace file '%s' should be readable"), *ExpectedFile));
		return false;
	}

	TestTrue(TEXT("Saved trace file should contain the step id"), SavedText.Contains(TEXT("Execution.01")));
	TestTrue(TEXT("Saved trace file should contain the recorded evidence"), SavedText.Contains(TEXT("Result: 42")));
	return true;
}

#endif
