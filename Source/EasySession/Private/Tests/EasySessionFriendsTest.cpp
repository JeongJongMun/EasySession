// Copyright Langerak. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "OnlineSubsystemNames.h"
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
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return false;
	}

	// This test verifies the NULL behavior - skip when another subsystem (e.g. Steam)
	// is active, since friends would then genuinely be supported.
	if (Subsystem->GetOnlineSubsystemName() != NULL_SUBSYSTEM)
	{
		AddInfo(FString::Printf(TEXT("Skipped: active subsystem is '%s', not NULL."), *Subsystem->GetOnlineSubsystemName().ToString()));
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return true;
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
	TestFalse(TEXT("ShowProfileUI unsupported on NULL"), Subsystem->ShowProfileUI(FEasySessionFriend()));

	EasySessionTest::DestroyGameInstance(GameInstance.Get());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
