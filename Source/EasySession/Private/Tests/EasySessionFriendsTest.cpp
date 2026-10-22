// Copyright (c) 2026 Langerak.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSocial.h"
#include "EasySessionStatics.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "OnlineSubsystemNames.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Friends test on the NULL subsystem: the read must complete with NotSupportedByService inside the call instead of crashing or hanging.
 * ShowInviteUI must report NotSupportedByService, and an invite or a profile request without a friend id must report InvalidParams.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionFriendsUnsupportedTest, "EasySession.Friends.NullSubsystemGracefulFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionFriendsUnsupportedTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> GameInstance(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(GameInstance);

	UEasySessionSubsystem* Subsystem = GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return false;
	}

	// This test verifies the NULL behavior, so it is skipped when another subsystem (e.g. Steam) is active, where friends are supported.
	const FName OnlineSubsystemName = UEasySessionStatics::GetOnlineSubsystemName(GameInstance.Get());
	if (OnlineSubsystemName != NULL_SUBSYSTEM)
	{
		AddInfo(FString::Printf(TEXT("Skipped: active subsystem is '%s', not NULL."), *OnlineSubsystemName.ToString()));
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return true;
	}

	// NULL has no friends interface. The callback must fire synchronously with a failure.
	bool bCallbackFired = false;
	Subsystem->ReadFriends(FEasyFriendsCompleteDelegate::CreateLambda(
		[this, &bCallbackFired](EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionFriend>& Friends)
		{
			bCallbackFired = true;
			TestEqual(TEXT("Read reports the online subsystem has no friends list"), Result, EEasySessionResult::NotSupportedByService);
			TestEqual(TEXT("No friends returned"), Friends.Num(), 0);
		}));
	TestTrue(TEXT("Friends callback fired"), bCallbackFired);
	TestFalse(TEXT("A refused read never made the subsystem busy"), Subsystem->IsBusy());

	// The invite helpers name the reason rather than returning a bare failure.
	TestEqual(TEXT("ShowInviteUI refuses with no session to invite to"), Subsystem->ShowInviteUI(), EEasySessionResult::NoSessionExists);
	TestEqual(TEXT("ShowProfileUI refuses a friend with no online id"), Subsystem->ShowProfileUI(FEasySessionFriend()), EEasySessionResult::InvalidParams);

	// A friend struct the friends list never returned is a caller mistake, not a missing feature.
	TestEqual(TEXT("An invite to no one is refused as an invalid parameter"), Subsystem->SendInviteToFriend(EEasySessionType::GameSession, FEasySessionFriend()), EEasySessionResult::InvalidParams);

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

	// NULL has no friends, so the queued lookup completes with that failure rather than with "no session".
	CurrentTest->TestEqual(TEXT("The queued friend lookup reports the online subsystem has no friends"), State->LookupResult.GetValue(), EEasySessionResult::NotSupportedByService);
	CurrentTest->TestEqual(TEXT("With no session for the friend"), State->DeliveredCount, 0);

	EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
	return true;
}

/**
 * The friend session lookup is a queue request like any other search: it occupies the queue while it runs, and completes through the request completion path.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionFriendLookupTest, "EasySession.Friends.FriendLookupRidesTheQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionFriendLookupTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionFriendLookupTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}


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
	LookupParams.FriendId = FUniqueNetIdRepl(FriendId);
	Subsystem->FindSessions(EEasySessionType::GameSession, LookupParams, FEasySessionFindCompleteDelegate::CreateLambda(
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

/**
 * The automatic join of an accepted invite has no node, so its result reaches the game only through the subsystem events.
 * A refusal for a missing password carries the session to join again with a password, and is not also reported as a failure.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionInvitedJoinResultTest, "EasySession.Social.InvitedJoinRefusalReachesTheGame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionInvitedJoinResultTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> GameInstance(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(GameInstance);

	UEasySessionSubsystem* Subsystem = GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return false;
	}

	TStrongObjectPtr<UEasySessionTestEventListener> Listener(NewObject<UEasySessionTestEventListener>());
	Subsystem->OnSessionFailure.AddDynamic(Listener.Get(), &UEasySessionTestEventListener::HandleSessionFailure);
	Subsystem->OnInvitePasswordRequired.AddDynamic(Listener.Get(), &UEasySessionTestEventListener::HandleInvitePasswordRequired);

	FEasySessionSearchResult Session;
	Session.SessionDisplayName = TEXT("Locked room");

	FEasySessionSocial::ReportInvitedJoinResult(*Subsystem, Session, EEasySessionResult::Success, FString());
	TestEqual(TEXT("A joined invite asks for nothing"), Listener->PasswordRequiredSessions.Num(), 0);
	TestEqual(TEXT("A joined invite reports no failure"), Listener->FailureReasons.Num(), 0);

	FEasySessionSocial::ReportInvitedJoinResult(*Subsystem, Session, EEasySessionResult::PasswordRequired, TEXT("This session needs a password."));
	if (TestEqual(TEXT("A missing password asks the game for one"), Listener->PasswordRequiredSessions.Num(), 1))
	{
		TestEqual(TEXT("The game gets the session to join again"), Listener->PasswordRequiredSessions[0].SessionDisplayName, Session.SessionDisplayName);
	}
	TestEqual(TEXT("A missing password is not also a failure"), Listener->FailureReasons.Num(), 0);

	FEasySessionSocial::ReportInvitedJoinResult(*Subsystem, Session, EEasySessionResult::JoinSessionFull, TEXT("The session is full."));
	TestEqual(TEXT("Any other refusal is a failure"), Listener->FailureReasons.Num(), 1);
	TestEqual(TEXT("Any other refusal asks for no password"), Listener->PasswordRequiredSessions.Num(), 1);

	EasySessionTest::DestroyGameInstance(GameInstance.Get());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
