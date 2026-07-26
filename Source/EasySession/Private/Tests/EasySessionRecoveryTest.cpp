// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Disconnect info test: recording, first-reason-wins overwrite protection,
 * and consume-once semantics.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionDisconnectInfoTest, "EasySession.Recovery.DisconnectInfoStoreConsume", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionDisconnectInfoTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
	GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return false;
	}

	TestFalse(TEXT("No pending info initially"), Subsystem->HasPendingDisconnectInfo());

	// Record a disconnect. There is no session and no menu map configured, so the
	// recovery flow only stores the info.
	Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::ConnectionLost, FText::FromString(TEXT("First reason")));
	TestTrue(TEXT("Pending after first notify"), Subsystem->HasPendingDisconnectInfo());

	// A follow-up failure must not overwrite the original cause.
	Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::TravelFailure, FText::FromString(TEXT("Second reason")));

	const FEasyDisconnectInfo Info = Subsystem->ConsumeLastDisconnectInfo();
	TestEqual(TEXT("First reason wins"), Info.Reason, EEasyDisconnectReason::ConnectionLost);
	TestEqual(TEXT("First reason text preserved"), Info.ReasonText.ToString(), FString(TEXT("First reason")));

	TestFalse(TEXT("Not pending after consume"), Subsystem->HasPendingDisconnectInfo());

	const FEasyDisconnectInfo EmptyInfo = Subsystem->ConsumeLastDisconnectInfo();
	TestEqual(TEXT("Second consume returns None"), EmptyInfo.Reason, EEasyDisconnectReason::None);

	// A new disconnect after consuming records fresh info again.
	Subsystem->NotifyDisconnectedFromSession(EEasyDisconnectReason::HostEndedSession, FText::FromString(TEXT("Third reason")));
	TestTrue(TEXT("Pending again after re-notify"), Subsystem->HasPendingDisconnectInfo());
	TestEqual(TEXT("New reason recorded"), Subsystem->ConsumeLastDisconnectInfo().Reason, EEasyDisconnectReason::HostEndedSession);

	EasySessionTest::DestroyGameInstance(GameInstance.Get());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
