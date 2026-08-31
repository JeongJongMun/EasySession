// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionOpenSlotsUpdateTest
{
	/** Maximum time to wait for each queued operation before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	/** Which step of the create/raise/lower sequence the latent command is waiting on. */
	enum class EStep : uint8
	{
		AwaitingCreate,
		AwaitingRaise,
		AwaitingLower,
		AwaitingDestroy,
		Done
	};

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		EStep Step = EStep::AwaitingCreate;
		TOptional<EEasySessionResult> PendingResult;
		double StartTime = 0.0;
	};

	static FEasySessionHostParams MakeParams()
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Open Slots Update");
		Params.MaxPlayers = 4;
		Params.bIsLANMatch = true;
		Params.bStartListening = false;
		return Params;
	}

	/** Take the result the last step produced, leaving the slot empty for the next one. */
	static EEasySessionResult ConsumeResult(FTestState& State)
	{
		const EEasySessionResult Result = State.PendingResult.GetValue();
		State.PendingResult.Reset();
		State.StartTime = FPlatformTime::Seconds();
		return Result;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForOpenSlotsUpdate, TSharedPtr<EasySessionOpenSlotsUpdateTest::FTestState>, State);
bool FEasySessionWaitForOpenSlotsUpdate::Update()
{
	using namespace EasySessionOpenSlotsUpdateTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

	// State is a member of this latent command, and a lambda cannot capture a member.
	// The completion delegates below need one they can hold, so take a local handle.
	TSharedPtr<FTestState> Shared = State;

	if (!State->PendingResult.IsSet())
	{
		if (FPlatformTime::Seconds() - State->StartTime > TimeoutSeconds)
		{
			CurrentTest->AddError(TEXT("Timed out waiting for a session operation to complete."));
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
		return false;
	}

	switch (State->Step)
	{
		case EStep::AwaitingCreate:
		{
			CurrentTest->TestEqual(TEXT("Session created"), ConsumeResult(*State), EEasySessionResult::Success);

			// The occupancy this whole test reasons about: however many players creation
			// registered, that many slots must be missing from the cap.
			const int32 Registered = FEasySessionTestAccess::GetRegisteredPlayerCount(*Subsystem);
			CurrentTest->TestEqual(TEXT("Open slots match the roster after create"),
				FEasySessionTestAccess::GetOpenPublicConnections(*Subsystem), 4 - Registered);

			// Raise the cap. The engine's UpdateSession does not touch the open slot
			// count, so without the plugin's recompute this would show a player that
			// never joined.
			FEasySessionSettings Raised = Subsystem->GetSessionSettings();
			Raised.MaxPlayers = 6;
			State->Step = EStep::AwaitingRaise;
			Subsystem->UpdateEasySession(Raised, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->PendingResult = Result;
				}));
			return false;
		}

		case EStep::AwaitingRaise:
		{
			CurrentTest->TestEqual(TEXT("Raising the cap succeeded"), ConsumeResult(*State), EEasySessionResult::Success);

			const int32 Registered = FEasySessionTestAccess::GetRegisteredPlayerCount(*Subsystem);
			CurrentTest->TestEqual(TEXT("Open slots match the roster after raising the cap"),
				FEasySessionTestAccess::GetOpenPublicConnections(*Subsystem), 6 - Registered);

			// Lower the cap below the roster. Open slots must clamp to zero, not go negative.
			FEasySessionSettings Lowered = Subsystem->GetSessionSettings();
			Lowered.MaxPlayers = Registered > 0 ? Registered - 1 : 0;
			if (Lowered.MaxPlayers <= 0)
			{
				// Nothing was registered, so there is no occupancy to squeeze below - skip to teardown.
				State->Step = EStep::AwaitingDestroy;
				Subsystem->LeaveEasySession(FEasySessionCompleteDelegate::CreateLambda(
					[Shared](EEasySessionResult Result, const FString&)
					{
						Shared->PendingResult = Result;
					}));
				return false;
			}
			State->Step = EStep::AwaitingLower;
			Subsystem->UpdateEasySession(Lowered, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->PendingResult = Result;
				}));
			return false;
		}

		case EStep::AwaitingLower:
		{
			CurrentTest->TestEqual(TEXT("Lowering the cap succeeded"), ConsumeResult(*State), EEasySessionResult::Success);

			CurrentTest->TestEqual(TEXT("Open slots clamp at zero when the cap drops below the roster"),
				FEasySessionTestAccess::GetOpenPublicConnections(*Subsystem), 0);

			State->Step = EStep::AwaitingDestroy;
			Subsystem->LeaveEasySession(FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->PendingResult = Result;
				}));
			return false;
		}

		case EStep::AwaitingDestroy:
		{
			CurrentTest->TestEqual(TEXT("Session destroyed"), ConsumeResult(*State), EEasySessionResult::Success);
			State->Step = EStep::Done;
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}

		default:
			return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionOpenSlotsFollowTheCapTest,
	"EasySession.Subsystem.UpdateKeepsOpenSlotsMatchingTheRoster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionOpenSlotsFollowTheCapTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionOpenSlotsUpdateTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	Subsystem->CreateEasySession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->PendingResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForOpenSlotsUpdate(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
