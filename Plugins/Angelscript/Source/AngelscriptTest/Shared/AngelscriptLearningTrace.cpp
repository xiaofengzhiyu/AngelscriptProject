#include "Shared/AngelscriptLearningTrace.h"

#include "HAL/FileManager.h"
#include "Logging/LogMacros.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAngelscriptLearningTrace, Log, All);

namespace AngelscriptTestSupport
{
	FAngelscriptLearningTraceSession::FAngelscriptLearningTraceSession(
		const FString& InSessionName,
		const FAngelscriptLearningTraceSinkConfig& InSinkConfig)
		: SessionName(InSessionName)
		, SinkConfig(InSinkConfig)
	{
	}

	void FAngelscriptLearningTraceSession::BeginPhase(EAngelscriptLearningTracePhase InPhase)
	{
		CurrentPhase = InPhase;
		CurrentPhaseStepIndex = 0;
	}

	void FAngelscriptLearningTraceSession::AddStep(const FString& Action, const FString& Observation)
	{
		++CurrentPhaseStepIndex;

		FAngelscriptLearningTraceEvent& Event = Events.AddDefaulted_GetRef();
		Event.SequenceIndex = NextSequenceIndex++;
		Event.Phase = CurrentPhase;
		Event.StepId = FString::Printf(TEXT("%s.%02d"), *GetPhaseLabel(CurrentPhase), CurrentPhaseStepIndex);
		Event.Action = Action;
		Event.Observation = Observation;
	}

	void FAngelscriptLearningTraceSession::AddKeyValue(const FString& Key, const FString& Value)
	{
		if (FAngelscriptLearningTraceEvent* Event = GetLastEvent())
		{
			Event->Evidence.Add({Key, Value});
		}
	}

	void FAngelscriptLearningTraceSession::AddCodeBlock(const FString& CodeBlock)
	{
		if (FAngelscriptLearningTraceEvent* Event = GetLastEvent())
		{
			Event->CodeBlocks.Add(CodeBlock);
		}
	}

	const TArray<FAngelscriptLearningTraceEvent>& FAngelscriptLearningTraceSession::GetEvents() const
	{
		return Events;
	}

	TArray<FString> FAngelscriptLearningTraceSession::BuildSummaryLines() const
	{
		TArray<FString> Lines;
		Lines.Reserve(Events.Num());
		for (const FAngelscriptLearningTraceEvent& Event : Events)
		{
			Lines.Add(BuildEventLine(Event));
		}
		return Lines;
	}

	void FAngelscriptLearningTraceSession::FlushToAutomation(FAutomationTestBase& Test) const
	{
		if (!SinkConfig.bEmitToAutomation)
		{
			return;
		}

		for (const FString& Line : BuildSummaryLines())
		{
			Test.AddInfo(Line);
		}
	}

	void FAngelscriptLearningTraceSession::FlushToLog() const
	{
		if (!SinkConfig.bEmitToLog)
		{
			return;
		}

		for (const FString& Line : BuildSummaryLines())
		{
			UE_LOG(LogAngelscriptLearningTrace, Display, TEXT("%s"), *Line);
		}
	}

	bool FAngelscriptLearningTraceSession::FlushToFile() const
	{
		if (!SinkConfig.bEmitToFile)
		{
			return true;
		}

		const FString OutputDirectory = GetOutputRoot();
		if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
		{
			return false;
		}

		FString OutputText;
		for (const FAngelscriptLearningTraceEvent& Event : Events)
		{
			if (!OutputText.IsEmpty())
			{
				OutputText += LINE_TERMINATOR;
			}

			OutputText += BuildEventLine(Event);
			for (const FAngelscriptLearningTraceKeyValue& Pair : Event.Evidence)
			{
				OutputText += LINE_TERMINATOR;
				OutputText += FString::Printf(TEXT("  - %s: %s"), *Pair.Key, *Pair.Value);
			}

			if (SinkConfig.DetailLevel == EAngelscriptLearningTraceDetailLevel::Verbose)
			{
				for (const FString& CodeBlock : Event.CodeBlocks)
				{
					OutputText += LINE_TERMINATOR;
					OutputText += CodeBlock;
				}
			}
		}

		const FString OutputPath = FPaths::Combine(OutputDirectory, SessionName + TEXT(".trace.txt"));
		return FFileHelper::SaveStringToFile(OutputText, *OutputPath);
	}

	const FAngelscriptLearningTraceEvent* FAngelscriptLearningTraceSession::GetLastEvent() const
	{
		return Events.Num() > 0 ? &Events.Last() : nullptr;
	}

	FAngelscriptLearningTraceEvent* FAngelscriptLearningTraceSession::GetLastEvent()
	{
		return Events.Num() > 0 ? &Events.Last() : nullptr;
	}

	FString FAngelscriptLearningTraceSession::BuildEventLine(const FAngelscriptLearningTraceEvent& Event) const
	{
		FString Line = FString::Printf(TEXT("[%03d] %s | %s | %s"), Event.SequenceIndex, *Event.StepId, *Event.Action, *Event.Observation);
		if (SinkConfig.DetailLevel == EAngelscriptLearningTraceDetailLevel::Verbose && Event.Evidence.Num() > 0)
		{
			for (const FAngelscriptLearningTraceKeyValue& Pair : Event.Evidence)
			{
				Line += FString::Printf(TEXT(" | %s=%s"), *Pair.Key, *Pair.Value);
			}
		}
		return Line;
	}

	FString FAngelscriptLearningTraceSession::GetPhaseLabel(EAngelscriptLearningTracePhase Phase)
	{
		switch (Phase)
		{
		case EAngelscriptLearningTracePhase::EngineBootstrap:
			return TEXT("EngineBootstrap");
		case EAngelscriptLearningTracePhase::Binding:
			return TEXT("Binding");
		case EAngelscriptLearningTracePhase::Compile:
			return TEXT("Compile");
		case EAngelscriptLearningTracePhase::Bytecode:
			return TEXT("Bytecode");
		case EAngelscriptLearningTracePhase::ClassGeneration:
			return TEXT("ClassGeneration");
		case EAngelscriptLearningTracePhase::Execution:
			return TEXT("Execution");
		case EAngelscriptLearningTracePhase::UEBridge:
			return TEXT("UEBridge");
		case EAngelscriptLearningTracePhase::Debug:
			return TEXT("Debug");
		case EAngelscriptLearningTracePhase::GC:
			return TEXT("GC");
		case EAngelscriptLearningTracePhase::Editor:
			return TEXT("Editor");
		default:
			return TEXT("Trace");
		}
	}

	FString FAngelscriptLearningTraceSession::GetOutputRoot() const
	{
		if (!SinkConfig.OutputRoot.IsEmpty())
		{
			return SinkConfig.OutputRoot;
		}

		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/AngelscriptLearning"));
	}
}
