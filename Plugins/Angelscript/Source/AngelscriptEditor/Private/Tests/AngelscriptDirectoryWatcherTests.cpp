#include "AngelscriptDirectoryWatcherInternal.h"

#include "AngelscriptEngine.h"

#include "HAL/FileManager.h"
#include "IDirectoryWatcher.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDirectoryWatcherScriptQueueTest,
	"Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDirectoryWatcherNonScriptIgnoreTest,
	"Angelscript.Editor.DirectoryWatcher.Queue.IgnoresNonScriptFiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDirectoryWatcherFolderAddTest,
	"Angelscript.Editor.DirectoryWatcher.Queue.FolderAddScansContainedScripts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDirectoryWatcherFolderRemoveTest,
	"Angelscript.Editor.DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDirectoryWatcherRenameWindowTest,
	"Angelscript.Editor.DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FString MakeTempWatcherRoot(const TCHAR* Prefix)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("DirectoryWatcherTests") / FString::Printf(TEXT("%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	}

	FFileChangeData MakeFileChange(const FString& Filename, FFileChangeData::EFileChangeAction Action)
	{
		return FFileChangeData(Filename, Action);
	}

	TUniquePtr<FAngelscriptEngine> MakeTestEngineWithRoot(const FString& RootPath)
	{
		const FAngelscriptEngineConfig Config;
		const FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
		TUniquePtr<FAngelscriptEngine> Engine = MakeUnique<FAngelscriptEngine>(Config, Dependencies);
		Engine->AllRootPaths = { FPaths::ConvertRelativePathToFull(RootPath) };
		return Engine;
	}

	bool ContainsFilenamePair(const TArray<FAngelscriptEngine::FFilenamePair>& Files, const FString& AbsolutePath, const FString& RelativePath)
	{
		for (const FAngelscriptEngine::FFilenamePair& File : Files)
		{
			if (File.AbsolutePath == AbsolutePath && File.RelativePath == RelativePath)
			{
				return true;
			}
		}

		return false;
	}
}

bool FAngelscriptDirectoryWatcherScriptQueueTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString RootPath = MakeTempWatcherRoot(TEXT("ScriptQueue"));
	ON_SCOPE_EXIT
	{
		FileManager.DeleteDirectory(*RootPath, false, true);
	};

	FileManager.MakeDirectory(*(RootPath / TEXT("Scripts")), true);
	TUniquePtr<FAngelscriptEngine> Engine = MakeTestEngineWithRoot(RootPath);

	const FString AddedAbsolutePath = FPaths::ConvertRelativePathToFull(RootPath / TEXT("Scripts/Added.as"));
	const FString RemovedAbsolutePath = FPaths::ConvertRelativePathToFull(RootPath / TEXT("Scripts/Removed.as"));
	const TArray<FFileChangeData> Changes = {
		MakeFileChange(AddedAbsolutePath, FFileChangeData::FCA_Added),
		MakeFileChange(RemovedAbsolutePath, FFileChangeData::FCA_Removed)
	};

	AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, [](const FString&)
	{
		return TArray<FAngelscriptEngine::FFilenamePair>();
	});

	TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
	TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);
	TestTrue(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should store the added script pair"), ContainsFilenamePair(Engine->FileChangesDetectedForReload, AddedAbsolutePath, TEXT("Scripts/Added.as")));
	return TestTrue(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should store the removed script pair"), ContainsFilenamePair(Engine->FileDeletionsDetectedForReload, RemovedAbsolutePath, TEXT("Scripts/Removed.as")));
}

bool FAngelscriptDirectoryWatcherNonScriptIgnoreTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString RootPath = MakeTempWatcherRoot(TEXT("NonScriptIgnore"));
	ON_SCOPE_EXIT
	{
		FileManager.DeleteDirectory(*RootPath, false, true);
	};

	FileManager.MakeDirectory(*(RootPath / TEXT("Misc")), true);
	TUniquePtr<FAngelscriptEngine> Engine = MakeTestEngineWithRoot(RootPath);

	const FString TextFilePath = FPaths::ConvertRelativePathToFull(RootPath / TEXT("Misc/Notes.txt"));
	const TArray<FFileChangeData> Changes = {
		MakeFileChange(TextFilePath, FFileChangeData::FCA_Modified)
	};

	AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, [](const FString&)
	{
		return TArray<FAngelscriptEngine::FFilenamePair>();
	});

	TestEqual(TEXT("DirectoryWatcher.Queue.IgnoresNonScriptFiles should not queue added scripts"), Engine->FileChangesDetectedForReload.Num(), 0);
	return TestEqual(TEXT("DirectoryWatcher.Queue.IgnoresNonScriptFiles should not queue removed scripts"), Engine->FileDeletionsDetectedForReload.Num(), 0);
}

bool FAngelscriptDirectoryWatcherFolderAddTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString RootPath = MakeTempWatcherRoot(TEXT("FolderAdd"));
	const FString AddedFolderPath = RootPath / TEXT("AddedFolder");
	ON_SCOPE_EXIT
	{
		FileManager.DeleteDirectory(*RootPath, false, true);
	};

	FileManager.MakeDirectory(*(AddedFolderPath / TEXT("Nested")), true);
	FFileHelper::SaveStringToFile(TEXT("// A"), *(AddedFolderPath / TEXT("A.as")));
	FFileHelper::SaveStringToFile(TEXT("ignore"), *(AddedFolderPath / TEXT("B.txt")));
	FFileHelper::SaveStringToFile(TEXT("// C"), *(AddedFolderPath / TEXT("Nested/C.as")));

	TUniquePtr<FAngelscriptEngine> Engine = MakeTestEngineWithRoot(RootPath);
	const TArray<FFileChangeData> Changes = {
		MakeFileChange(FPaths::ConvertRelativePathToFull(AddedFolderPath), FFileChangeData::FCA_Added)
	};

	AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, [](const FString&)
	{
		return TArray<FAngelscriptEngine::FFilenamePair>();
	});

	TestEqual(TEXT("DirectoryWatcher.Queue.FolderAddScansContainedScripts should queue the two script files in the new folder"), Engine->FileChangesDetectedForReload.Num(), 2);
	TestTrue(TEXT("DirectoryWatcher.Queue.FolderAddScansContainedScripts should queue AddedFolder/A.as"), ContainsFilenamePair(Engine->FileChangesDetectedForReload, FPaths::ConvertRelativePathToFull(AddedFolderPath / TEXT("A.as")), TEXT("AddedFolder/A.as")));
	return TestTrue(TEXT("DirectoryWatcher.Queue.FolderAddScansContainedScripts should queue AddedFolder/Nested/C.as"), ContainsFilenamePair(Engine->FileChangesDetectedForReload, FPaths::ConvertRelativePathToFull(AddedFolderPath / TEXT("Nested/C.as")), TEXT("AddedFolder/Nested/C.as")));
}

bool FAngelscriptDirectoryWatcherFolderRemoveTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString RootPath = MakeTempWatcherRoot(TEXT("FolderRemove"));
	const FString RemovedFolderPath = FPaths::ConvertRelativePathToFull(RootPath / TEXT("RemovedFolder"));
	ON_SCOPE_EXIT
	{
		FileManager.DeleteDirectory(*RootPath, false, true);
	};

	FileManager.MakeDirectory(*RootPath, true);
	TUniquePtr<FAngelscriptEngine> Engine = MakeTestEngineWithRoot(RootPath);

	const FString RemovedScriptAbsolutePath = RemovedFolderPath / TEXT("RemovedA.as");
	const FString NestedRemovedScriptAbsolutePath = RemovedFolderPath / TEXT("Nested/RemovedB.as");
	const TArray<FFileChangeData> Changes = {
		MakeFileChange(RemovedFolderPath, FFileChangeData::FCA_Removed)
	};

	AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, [&](const FString& AbsoluteFolderPath)
	{
		TestEqual(TEXT("DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator should request the removed folder path with a trailing separator"), AbsoluteFolderPath, RemovedFolderPath / TEXT(""));
		return TArray<FAngelscriptEngine::FFilenamePair>{
			{ RemovedScriptAbsolutePath, TEXT("RemovedFolder/RemovedA.as") },
			{ NestedRemovedScriptAbsolutePath, TEXT("RemovedFolder/Nested/RemovedB.as") },
		};
	});

	TestEqual(TEXT("DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator should queue two removed scripts from the enumerator"), Engine->FileDeletionsDetectedForReload.Num(), 2);
	TestTrue(TEXT("DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator should queue the direct removed script"), ContainsFilenamePair(Engine->FileDeletionsDetectedForReload, RemovedScriptAbsolutePath, TEXT("RemovedFolder/RemovedA.as")));
	return TestTrue(TEXT("DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator should queue the nested removed script"), ContainsFilenamePair(Engine->FileDeletionsDetectedForReload, NestedRemovedScriptAbsolutePath, TEXT("RemovedFolder/Nested/RemovedB.as")));
}

bool FAngelscriptDirectoryWatcherRenameWindowTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString RootPath = MakeTempWatcherRoot(TEXT("RenameWindow"));
	const FString RenameFolderPath = RootPath / TEXT("Rename");
	ON_SCOPE_EXIT
	{
		FileManager.DeleteDirectory(*RootPath, false, true);
	};

	FileManager.MakeDirectory(*RenameFolderPath, true);
	TUniquePtr<FAngelscriptEngine> Engine = MakeTestEngineWithRoot(RootPath);

	const FString OldAbsolutePath = FPaths::ConvertRelativePathToFull(RenameFolderPath / TEXT("OldName.as"));
	const FString NewAbsolutePath = FPaths::ConvertRelativePathToFull(RenameFolderPath / TEXT("NewName.as"));
	const TArray<FFileChangeData> Changes = {
		MakeFileChange(OldAbsolutePath, FFileChangeData::FCA_Removed),
		MakeFileChange(NewAbsolutePath, FFileChangeData::FCA_Added)
	};

	AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, [](const FString&)
	{
		return TArray<FAngelscriptEngine::FFilenamePair>();
	});

	TestEqual(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);
	TestEqual(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
	TestTrue(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should retain the old filename in the deletion queue"), ContainsFilenamePair(Engine->FileDeletionsDetectedForReload, OldAbsolutePath, TEXT("Rename/OldName.as")));
	return TestTrue(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should retain the new filename in the addition queue"), ContainsFilenamePair(Engine->FileChangesDetectedForReload, NewAbsolutePath, TEXT("Rename/NewName.as")));
}

#endif
