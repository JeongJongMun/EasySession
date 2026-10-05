// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionSearchFilterTest
{
	// Maximum time to wait for each step before failing the test.
	static constexpr double TimeoutSeconds = 20.0;

	/** The code the search asks for, typed in lower case the way a player might. */
	static const TCHAR* TypedJoinCode = TEXT("abc2de");

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TOptional<EEasySessionResult> FindResult;

		enum class EStep { AwaitingCreate, AwaitingDestroy, AwaitingSearch, AwaitingFind };
		EStep Step = EStep::AwaitingCreate;
		double StartTime = 0.0;
	};

	// Whether the step timed out. Reports the error and destroys the game instance when it did.
	bool TimedOut(TSharedPtr<FTestState> State, const TCHAR* What)
	{
		if (FPlatformTime::Seconds() - State->StartTime <= TimeoutSeconds)
		{
			return false;
		}

		FAutomationTestFramework::Get().GetCurrentTest()->AddError(FString::Printf(TEXT("Timed out waiting for %s."), What));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	// An id for a made-up host, which the NULL subsystem creates for any name.
	FUniqueNetIdRepl MakeHostId(UWorld* World)
	{
		const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(World);
		return FUniqueNetIdRepl(Identity.IsValid() ? Identity->CreateUniquePlayerId(TEXT("EasySessionSearchedHost")) : nullptr);
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionSearchFilterStep, TSharedPtr<EasySessionSearchFilterTest::FTestState>, State);
bool FEasySessionSearchFilterStep::Update()
{
	using namespace EasySessionSearchFilterTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = State->GameInstance->GetWorld();

	switch (State->Step)
	{
		case FTestState::EStep::AwaitingCreate:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the create"));
			}

			const IOnlineSessionPtr Sessions = Online::GetSessionInterface(World);
			const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
			if (CurrentTest->TestNotNull(TEXT("The session exists"), NamedSession))
			{
				FString AdvertisedOwner;
				const bool bAdvertised = NamedSession->SessionSettings.Get(EasySession::SettingKey_OwnerId, AdvertisedOwner);

				// A headless test may create the session without a logged in player, and then there is no id to advertise.
				if (NamedSession->OwningUserId.IsValid())
				{
					CurrentTest->TestTrue(TEXT("The session advertises its owner"), bAdvertised);
					CurrentTest->TestEqual(TEXT("The advertised owner is the player the session was created for"), AdvertisedOwner, NamedSession->OwningUserId->ToString());
				}
				else
				{
					CurrentTest->TestFalse(TEXT("A session without an owner advertises none"), bAdvertised);
				}

				CurrentTest->TestTrue(TEXT("The owner key is kept out of Custom Settings"), EasySession::IsReservedSettingKey(EasySession::SettingKey_OwnerId));
			}

			Subsystem->DestroySession();
			State->Step = FTestState::EStep::AwaitingDestroy;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingDestroy:
		{
			if (Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the destroy"));
			}

			FEasySessionSearchParams Params;
			Params.bLANQuery = true;
			Params.OwnerId = MakeHostId(World);
			Params.JoinCode = TypedJoinCode;

			TSharedPtr<FTestState> Shared = State;
			Subsystem->FindSessions(Params, FEasySessionFindCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString& /*ErrorMessage*/, const TArray<FEasySessionSearchResult>& /*Results*/)
				{
					Shared->FindResult = Result;
				}));

			State->Step = FTestState::EStep::AwaitingSearch;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingSearch:
		{
			// The queue starts the search on its next tick.
			const TSharedPtr<FOnlineSessionSearch> Search = FEasySessionTestAccess::GetActiveSearch(*Subsystem);
			if (!Search.IsValid())
			{
				return TimedOut(State, TEXT("the search to start"));
			}

			FString QueriedOwner;
			CurrentTest->TestTrue(TEXT("The owner filter goes to the online service"), Search->QuerySettings.Get(EasySession::SettingKey_OwnerId, QueriedOwner));
			CurrentTest->TestEqual(TEXT("It asks for the searched host"), QueriedOwner, MakeHostId(World).ToString());

			FString QueriedCode;
			CurrentTest->TestTrue(TEXT("The join code filter goes to the online service"), Search->QuerySettings.Get(EasySession::SettingKey_JoinCode, QueriedCode));
			CurrentTest->TestEqual(TEXT("It asks for the code in the case codes are advertised in"), QueriedCode, FString(TypedJoinCode).ToUpper());

			FEasySessionTestAccess::DriveFindCompletion(*Subsystem, {});
			State->Step = FTestState::EStep::AwaitingFind;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case FTestState::EStep::AwaitingFind:
		{
			if (!State->FindResult.IsSet() || Subsystem->IsBusy())
			{
				return TimedOut(State, TEXT("the search to complete"));
			}

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}

	return true;
}

/**
 * A search for one host or one join code filters inside the online subsystem, not only on the results it returned.
 * The online subsystem returns a limited number of sessions, so with many sessions the searched session could be missing from them.
 * The session advertises its owner for the owner filter to match, and the owner key stays out of CustomSettings like every key the plugin writes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionSearchFilterTest, "EasySession.Search.OneSessionQueriesFilterOnTheService", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionSearchFilterTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionSearchFilterTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasySessionHostParams HostParams;
	HostParams.SessionDisplayName = TEXT("EasySession Search Filter");
	HostParams.bIsLANMatch = true;
	HostParams.InitialMapName = EasySessionTest::SessionMapName;
	Subsystem->CreateSession(HostParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionSearchFilterStep(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
