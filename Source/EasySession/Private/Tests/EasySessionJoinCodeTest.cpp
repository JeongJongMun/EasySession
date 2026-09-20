// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

/**
 * The generated code is safe to read aloud and type: six characters, none of them look-alikes (no 0/O, 1/I/L, 8/B), and two codes in a row are not the same.
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

	// A collision is one in seven hundred million, so two equal codes mean the generator is broken, not unlucky.
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

		/** A joinable but unreachable result copied from the hidden session. */
		FOnlineSessionSearchResult BaseResult;

		/** The code the hidden session advertises. */
		FString JoinCode;

		/** What the code-filtered search delivered, the preview a UI would show. */
		TArray<FEasySessionSearchResult> PreviewResults;

		TOptional<int32> NormalSearchCount;
		bool bPreviewDelivered = false;
		TOptional<EEasySessionResult> JoinResult;
		TOptional<int32> WrongCodeCount;
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

			const FEasySessionSettings ReadBack = Subsystem->GetSessionSettings();
			CurrentTest->TestTrue(TEXT("The code toggle reads back"), ReadBack.bUseJoinCode);
			CurrentTest->TestTrue(TEXT("Hidden reads back"), ReadBack.bHidden);

			State->BaseResult = FEasySessionTestAccess::MakeSearchResultFromCurrentSession(*Subsystem);
			if (!CurrentTest->TestTrue(TEXT("The crafted result is joinable"), State->BaseResult.IsValid()))
			{
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			// A normal search must not list the hidden session, code or no code.
			FEasySessionSearchParams NormalSearch;
			NormalSearch.bLANQuery = true;
			Subsystem->FindSessions(NormalSearch, FEasySessionFindCompleteDelegate::CreateLambda(
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

			CurrentTest->TestEqual(TEXT("A normal search does not list the hidden session"), State->NormalSearchCount.GetValue(), 0);

			// The code-filtered search is the preview: the one hidden session, passed to the requester.
			FEasySessionSearchParams CodeSearch;
			CodeSearch.bLANQuery = true;
			CodeSearch.JoinCode = State->JoinCode;
			Subsystem->FindSessions(CodeSearch, FEasySessionFindCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>& Results)
				{
					Shared->PreviewResults = Results;
					Shared->bPreviewDelivered = true;
				}));

			State->Phase = 2;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 2:
		{
			if (!State->bPreviewDelivered)
			{
				FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->BaseResult });
				return false;
			}

			CurrentTest->TestEqual(TEXT("The code search previews the hidden session"), State->PreviewResults.Num(), 1);
			if (State->PreviewResults.Num() != 1)
			{
				EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
				return true;
			}

			// A read-modify-write update must not lose the code.
			Subsystem->UpdateSession(Subsystem->GetSessionSettings());
			State->Phase = 3;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 3:
		{
			if (Subsystem->IsBusy())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("The update kept the code"), Subsystem->GetSessionJoinCode(), State->JoinCode);

			// The record must be gone before joining the preview, or the join is refused for being in a session already.
			Subsystem->DestroySession();
			State->Phase = 4;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 4:
		{
			if (Subsystem->IsInSession() || Subsystem->IsBusy())
			{
				return false;
			}

			// The previewed result joins like any other search result.
			Subsystem->JoinSession(State->PreviewResults[0], FString(), FString(), FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->JoinResult = Result;
				}));

			State->Phase = 5;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		case 5:
		{
			if (!State->JoinResult.IsSet() || Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			// ResolveFailure, not InvalidParams: the preview was a real joinable target and a join was really tried.
			CurrentTest->TestEqual(TEXT("The previewed session was genuinely joined"), State->JoinResult.GetValue(), EEasySessionResult::ResolveFailure);

			// A code no session advertises finds nothing, even with the session in the results.
			FEasySessionSearchParams WrongSearch;
			WrongSearch.bLANQuery = true;
			WrongSearch.JoinCode = TEXT("QQQQQQ");
			Subsystem->FindSessions(WrongSearch, FEasySessionFindCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&, const TArray<FEasySessionSearchResult>& Results)
				{
					Shared->WrongCodeCount = Result == EEasySessionResult::Success ? Results.Num() : -1;
				}));

			State->Phase = 6;
			State->StartTime = FPlatformTime::Seconds();
			return false;
		}

		default:
		{
			if (!State->WrongCodeCount.IsSet())
			{
				FEasySessionTestAccess::DriveFindCompletion(*Subsystem, { State->BaseResult });
				return false;
			}
			if (Subsystem->IsBusy() || Subsystem->IsInSession())
			{
				return false;
			}

			CurrentTest->TestEqual(TEXT("A wrong code finds nothing"), State->WrongCodeCount.GetValue(), 0);

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * The whole life of a join code against a hidden session.
 * It is advertised and readable by the host, invisible to a normal search, and preserved by a read-modify-write update.
 * A code search then previews it, and that result joins like any other.
 * The join itself fails on address resolve, which is what proves the previewed session was real rather than skipped.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionJoinCodeTest, "EasySession.JoinCode.CodeOpensAHiddenSession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionJoinCodeTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionJoinCodeTest;

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
	HostParams.SessionDisplayName = TEXT("EasySession Join Code Host");
	HostParams.bIsLANMatch = true;
	HostParams.InitialMapName = EasySessionTest::SessionMapName;
	HostParams.bHidden = true;
	HostParams.bUseJoinCode = true;
	Subsystem->CreateSession(HostParams);

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForJoinCodeRun(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
