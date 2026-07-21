// Copyright Langerak. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Friends test on the NULL subsystem: the read must fail gracefully with a
 * clear result instead of crashing or hanging, and invite helpers must
 * report unsupported.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionFriendsUnsupportedTest, "EasySession.Friends.NullSubsystemGracefulFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionFriendsUnsupportedTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
	GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		GameInstance->Shutdown();
		return false;
	}

	// NULL has no friends interface - the callback must fire synchronously with a failure.
	bool bCallbackFired = false;
	Subsystem->ReadFriends(FEasyFriendsCompleteDelegate::CreateLambda(
		[this, &bCallbackFired](EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionFriend>& Friends)
		{
			bCallbackFired = true;
			TestNotEqual(TEXT("Read fails on NULL"), Result, EEasySessionResult::Success);
			TestEqual(TEXT("No friends returned"), Friends.Num(), 0);
		}));
	TestTrue(TEXT("Friends callback fired"), bCallbackFired);

	// Invite helpers must report unsupported instead of crashing.
	TestFalse(TEXT("ShowInviteUI unsupported on NULL"), Subsystem->ShowInviteUI());
	TestFalse(TEXT("SendSessionInviteToFriend fails without a valid friend"), Subsystem->SendSessionInviteToFriend(FEasySessionFriend()));
	TestEqual(TEXT("No pending invites"), Subsystem->GetPendingSessionInvites().Num(), 0);

	GameInstance->Shutdown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
