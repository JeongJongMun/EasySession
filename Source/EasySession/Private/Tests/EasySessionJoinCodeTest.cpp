// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

/**
 * The generated code is safe to read aloud and type: six characters, none of them
 * look-alikes (no 0/O, 1/I/L, 8/B), and two codes in a row are not the same room key.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionJoinCodeAlphabetTest, "EasySession.JoinCode.GeneratedCodeIsReadable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionJoinCodeAlphabetTest::RunTest(const FString& Parameters)
{
	const FString Alphabet = TEXT("23456789ACDEFGHJKMNPQRSTUVWXYZ");

	const FString First = EasySession::GenerateJoinCode();
	TestEqual(TEXT("The code has six characters"), First.Len(), 6);
	for (const TCHAR Char : First)
	{
		TestTrue(FString::Printf(TEXT("'%c' comes from the unambiguous alphabet"), Char), Alphabet.Contains(FString::Chr(Char)));
	}

	// A collision is one in seven hundred million - two equal codes mean the generator is broken, not unlucky.
	TestNotEqual(TEXT("Two generated codes differ"), First, EasySession::GenerateJoinCode());
	return true;
}

namespace EasySessionJoinCodeTest
{
	/** Maximum time to wait for each step before failing. */
	static constexpr double TimeoutSeconds = 30.0;

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		/** Kept alive for the latent command - a listener local to RunTest would be collected mid-run. */
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;

		/** A joinable-but-unreachable result borrowed from the hidden session. */
		FOnlineSessionSearchResult BaseResult;

		/** The code the hidden session advertises. */
		FString JoinCode;

		TOptional<int32> NormalSearchCount;
		TOptional<EEasySessionResult> ByCodeResult;
		TOptional<EEasySessionResult> WrongCodeResult;
		int32 Phase = 0;
		double StartTime = 0.0;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForJoinCodeRun, TSharedPtr<EasySessionJoinCodeTest::FTestState>, State);
bool FEasySessionWaitForJoinCodeRun::Update()
{
	using namespace EasySessionJoinCodeTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	TSharedPtr<FTestState> Shared = State;

	if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
	{
		CurrentTest->AddError(FString::Printf(TEXT("Timed out in phase %d. Queue: %s"), State->Phase, *Subsystem->GetQueueStatus()));
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}

	switch (State->Phase)
	{
		case 0:
		{
			if (!Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return false;
			}

			State->JoinCode = Subsystem->GetSessionJoinCode();
			CurrentTest->TestEqual(TEXT("The hidden session advertises a six character code"), State->JoinCode.Len(), 6);

			const FEasySessionHostParams ReadBack = Subsystem->GetSessionHostParams();
			CurrentTest->TestTrue(TEXT("The code toggle reads back"), ReadBack.bUseJoinCode);
			CurrentTest->TestTrue(TEXT("Hidden reads back"), ReadBack.bHidden);

			State->BaseResult = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem);
			if (!CurrentTest->TestTrue(TEXT("The crafted result is joinable"), State->BaseResult.IsValid()))
			{
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			// A normal search must not list the hidden room, code or no code.
			FEasySessionSearchParams NormalSearch;
			NormalSearch.bLANQuery = true;
			Subsystem->FindEasySessions(NormalSearch, FEasySessionFindCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>& Results)
				{
					Shared->NormalSearchCount = Result == EEasySessionResult::Success ? Results.Num() : -1;
				}));

			State->Phase = 1;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 1:
		{
			// The queue executes on its own tick, so keep offering the crafted result until the find takes it.
			if (!State->NormalSearchCount.IsSet())
			{
				FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->BaseResult });
				return false;
			}

			CurrentTest->TestEqual(TEXT("A normal search does not list the hidden room"), State->NormalSearchCount.GetValue(), 0);
			CurrentTest->TestEqual(TEXT("The normal search reached the public event"), State->Listener->FoundBroadcasts(), 1);

			// A read-modify-write update must not lose the code.
			Subsystem->UpdateEasySession(Subsystem->GetSessionHostParams());
			State->Phase = 2;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 2:
		{
			if (Subsystem->IsBusy())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("The update kept the code"), Subsystem->GetSessionJoinCode(), State->JoinCode);

			// The record must be gone before joining by code, or the join is refused for being in a session already.
			Subsystem->DestroyEasySession();
			State->Phase = 3;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 3:
		{
			if (Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return false;
			}

			Subsystem->JoinEasySessionByCode(State->JoinCode, FString(), FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->ByCodeResult = Result;
				}));

			State->Phase = 4;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 4:
		{
			if (!State->ByCodeResult.IsSet())
			{
				FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->BaseResult });
				return false;
			}
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			// ResolveFailure, not NoSessionsFound: the code found the hidden room and a join was genuinely tried.
			CurrentTest->TestEqual(TEXT("The code reached the hidden room"), State->ByCodeResult.GetValue(), EEasySessionResult::ResolveFailure);
			CurrentTest->TestEqual(TEXT("The code lookup stayed off the public event"), State->Listener->FoundBroadcasts(), 1);
			CurrentTest->TestEqual(TEXT("And off the public cache"), Subsystem->GetLastSearchResults().Num(), 0);

			// A code nobody advertises finds nothing, even with the room in the results.
			Subsystem->JoinEasySessionByCode(TEXT("QQQQQQ"), FString(), FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->WrongCodeResult = Result;
				}));

			State->Phase = 5;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		default:
		{
			if (!State->WrongCodeResult.IsSet())
			{
				FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->BaseResult });
				return false;
			}
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("A wrong code finds nothing"), State->WrongCodeResult.GetValue(), EEasySessionResult::NoSessionsFound);

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * The whole life of a join code against a hidden room: advertised and readable by the
 * host, invisible to a normal search, preserved by a read-modify-write update, and the
 * one key that lets Join Easy Session By Code reach the room - without its lookup ever
 * touching the public search surfaces. The join itself fails on address resolve, which
 * is what proves the room was found rather than skipped.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionJoinCodeTest, "EasySession.JoinCode.CodeOpensAHiddenRoom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionJoinCodeTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionJoinCodeTest;

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

	FEasySessionHostParams HostParams;
	HostParams.SessionDisplayName = TEXT("EasySession Join Code Host");
	HostParams.bIsLANMatch = true;
	HostParams.bStartListening = false;
	HostParams.bHidden = true;
	HostParams.bUseJoinCode = true;
	Subsystem->CreateEasySession(HostParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForJoinCodeRun(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
