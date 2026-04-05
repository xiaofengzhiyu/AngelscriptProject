#include "../Shared/AngelscriptTestMacros.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGlobalBindingsMacroValidationTest,
	"Angelscript.TestModule.Validation.GlobalBindingsMacro",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptGlobalBindingsMacroValidationTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	int32 Result = 0;
	ASTEST_COMPILE_RUN_INT(
		Engine,
		"ASGlobalVariableCompatMacro",
		TEXT(R"(
int Entry()
{
	if (CollisionProfile::BlockAllDynamic.Compare(FName("BlockAllDynamic")) != 0)
		return 10;

	FComponentQueryParams FreshParams;
	if (FComponentQueryParams::DefaultComponentQueryParams.ShapeCollisionMask.Bits != FreshParams.ShapeCollisionMask.Bits)
		return 20;

	FGameplayTag EmptyTagCopy = FGameplayTag::EmptyTag;
	if (EmptyTagCopy.IsValid())
		return 30;
	if (!FGameplayTagContainer::EmptyContainer.IsEmpty())
		return 40;
	if (!FGameplayTagQuery::EmptyQuery.IsEmpty())
		return 50;

	return 1;
}
		)"),
		TEXT("int Entry()"),
		Result);

	bPassed = TestEqual(TEXT("Global variable compat operations via macro should preserve bound namespace globals and defaults"), Result, 1);

	ASTEST_END_FULL
	return bPassed;
}

#endif
