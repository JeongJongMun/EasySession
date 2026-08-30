// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestEventListener.h"
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

	// The friend session sweep starts with the same read, so it must fail the same way.
	bool bSessionsCallbackFired = false;
	Subsystem->FindEasyFriendSessions(FEasyFriendSessionsCompleteDelegate::CreateLambda(
		[this, &bSessionsCallbackFired](EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasyFriendSession>& FriendSessions)
		{
			bSessionsCallbackFired = true;
			TestNotEqual(TEXT("Friend session search fails on NULL"), Result, EEasySessionResult::Success);
			TestEqual(TEXT("No friend sessions returned"), FriendSessions.Num(), 0);
		}));
	TestTrue(TEXT("Friend sessions callback fired"), bSessionsCallbackFired);

	// Invite helpers must report unsupported instead of crashing.
	TestFalse(TEXT("ShowInviteUI unsupported on NULL"), Subsystem->ShowInviteUI());
	TestFalse(TEXT("SendSessionInviteToFriend fails without a valid friend"), Subsystem->SendSessionInviteToFriend(FEasySessionFriend()));
	TestFalse(TEXT("ShowProfileUI unsupported on NULL"), Subsystem->ShowProfileUI(FEasySessionFriend()));

	EasySessionTest::DestroyGameInstance(GameInstance.Get());
	return true;
}

namespace EasySessionFriendLookupTest
{
	/** Maximum time to wait before failing. */
	static constexpr double TimeoutSeconds = 30.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** Kept alive for the latent command - a listener local to RunTest would be collected mid-run. */
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;

		TOptional<EEasySessionResult> LookupResult;
		int32 DeliveredCount = -1;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForFriendLookup, TSharedPtr<EasySessionFriendLookupTest::FTestState>, State);
bool FEasySessionWaitForFriendLookup::Update()
{
	using namespace EasySessionFriendLookupTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(TEXT("Timed out. Queue: %s"), *Subsystem->GetQueueStatus()));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	if (!State->LookupResult.IsSet() || Subsystem->IsBusy())
	{
		return false;
	}

	// NULL answers the friend query inside the call with "no session" - the queued request completes cleanly instead of erroring.
	CurrentTest->TestEqual(TEXT("The queued friend lookup completes"), State->LookupResult.GetValue(), EEasySessionResult::Success);
	CurrentTest->TestEqual(TEXT("With no session for the friend"), State->DeliveredCount, 0);
	CurrentTest->TestEqual(TEXT("And off the public search event"), State->Listener->FoundBroadcasts(), 0);
	CurrentTest->TestEqual(TEXT("And off the public cache"), Subsystem->GetLastSearchResults().Num(), 0);

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * The friend session lookup is a queue request like any other search: it occupies the
 * queue while it runs, completes through the request completion path, and - as a
 * hidden-seeing search - never reaches the public search event or cache.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionFriendLookupTest, "EasySession.Friends.FriendLookupRidesTheQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionFriendLookupTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionFriendLookupTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	State->Listener = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>());
	Subsystem->OnSessionsFound.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleSessionsFound);

	UWorld* World = State->GameInstance->GetWorld();
	const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(World);
	const FUniqueNetIdPtr FriendId = Identity.IsValid() ? Identity->CreateUniquePlayerId(TEXT("EasySessionFakeFriend")) : nullptr;
	if (!TestTrue(TEXT("A fake friend id could be made"), FriendId.IsValid()))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	TSharedPtr<FTestState> Shared = State;
	FEasySessionSearchParams LookupParams;
	LookupParams.SearchMode = EEasySessionSearchMode::ByFriend;
	LookupParams.SearchTargetId = FUniqueNetIdRepl(FriendId);
	Subsystem->FindEasySessions(LookupParams, FEasySessionFindCompleteDelegate::CreateLambda(
		[Shared](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>& Results)
		{
			Shared->LookupResult = Result;
			Shared->DeliveredCount = Results.Num();
		}));

	TestTrue(TEXT("The lookup occupies the queue"), Subsystem->IsBusy());

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForFriendLookup(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
