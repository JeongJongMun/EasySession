// Copyright Langerak. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionDiagnostics.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Diagnostics smoke test: the checks must run to completion on any subsystem
 * (NULL or Steam) without crashing, including a null world.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionDiagnosticsSmokeTest, "EasySession.Diagnostics.RunsToCompletion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionDiagnosticsSmokeTest::RunTest(const FString& Parameters)
{
	// Null world: must not crash.
	EasySessionDiagnostics::RunDiagnostics(nullptr);

	// Real (standalone) world: must run all checks to completion.
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
	GameInstance->InitializeStandalone();
	EasySessionDiagnostics::RunDiagnostics(GameInstance->GetWorld());
	EasySessionTest::DestroyGameInstance(GameInstance.Get());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
