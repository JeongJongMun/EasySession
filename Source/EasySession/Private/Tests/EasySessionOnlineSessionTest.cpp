// Copyright (c) 2026 Langerak.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionDiagnostics.h"
#include "EasySessionOnlineSession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionOnlineSessionTest
{
	/** Maximum time to wait for the whole sequence before failing. */
	static constexpr double TimeoutSeconds = 20.0;

	/** Whether the diagnostics report asks for the EasySession online session. */
	static bool AsksForOnlineSession(const EasySessionDiagnostics::FReport& Report)
	{
		return Report.Findings.ContainsByPredicate([](const EasySessionDiagnostics::FFinding& Finding)
		{
			return Finding.Message.Contains(TEXT("EasySessionOnlineSession"));
		});
	}

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;

		TOptional<EEasySessionResult> CreateResult;
		int32 Phase = 0;
		double StartTime = 0.0;
	};
}

/**
 * UEasySessionGameInstance gives the game the EasySession online session, and the diagnostics ask for it exactly when a GameInstance does not.
 * Without it every request is refused with NoEasySessionGameInstance inside the call, before anything is queued.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionOnlineSessionClassTest, "EasySession.OnlineSession.GameInstanceProvidesIt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionOnlineSessionClassTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionOnlineSessionTest;

	TStrongObjectPtr<UGameInstance> EasyInstance(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(EasyInstance);
	TestTrue(TEXT("The EasySession GameInstance creates the EasySession online session"), Cast<UEasySessionOnlineSession>(EasyInstance->GetOnlineSession()) != nullptr);
	TestFalse(TEXT("The diagnostics ask for nothing when it is used"), AsksForOnlineSession(EasySessionDiagnostics::RunDiagnostics(EasyInstance->GetWorld())));
	EasySessionTest::DestroyGameInstance(EasyInstance.Get());

	TStrongObjectPtr<UGameInstance> PlainInstance(NewObject<UGameInstance>(GEngine));
	EasySessionTest::InitializeGameInstance(PlainInstance);
	TestTrue(TEXT("The diagnostics ask for it on a plain GameInstance"), AsksForOnlineSession(EasySessionDiagnostics::RunDiagnostics(PlainInstance->GetWorld())));

	AddExpectedError(TEXT("The GameInstance does not use EasySessionOnlineSession"), EAutomationExpectedErrorFlags::Contains, 1);
	UEasySessionSubsystem* PlainSubsystem = PlainInstance->GetSubsystem<UEasySessionSubsystem>();
	TOptional<EEasySessionResult> PlainResult;
	PlainSubsystem->CreateSession(FEasySessionHostParams(), FEasySessionCompleteDelegate::CreateLambda([&PlainResult](EEasySessionResult Result, const FString&)
	{
		PlainResult = Result;
	}));
	TestTrue(TEXT("A request is refused with NoEasySessionGameInstance inside the call"), PlainResult.IsSet() && PlainResult.GetValue() == EEasySessionResult::NoEasySessionGameInstance);
	TestFalse(TEXT("The refused request is not queued"), PlainSubsystem->IsBusy());
	EasySessionTest::DestroyGameInstance(PlainInstance.Get());

	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForEngineDisconnect, TSharedPtr<EasySessionOnlineSessionTest::FTestState>, State);
bool FEasySessionWaitForEngineDisconnect::Update()
{
	using namespace EasySessionOnlineSessionTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

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
			if (!State->CreateResult.IsSet() || Subsystem->IsBusy())
			{
				return false;
			}
			CurrentTest->TestEqual(TEXT("Session created"), State->CreateResult.GetValue(), EEasySessionResult::Success);

			// A client loses its host.
			// The engine reports it to the online session twice, as it does when the net driver closes too.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false);
			UOnlineSession* OnlineSession = State->GameInstance->GetOnlineSession();
			OnlineSession->HandleDisconnect(State->GameInstance->GetWorld(), nullptr);
			OnlineSession->HandleDisconnect(State->GameInstance->GetWorld(), nullptr);

			CurrentTest->TestEqual(TEXT("One destroy is queued for both reports"),
				FEasySessionTestAccess::CountRequests(*Subsystem, FEasySessionRequest::EType::Destroy), 1);
			CurrentTest->TestTrue(TEXT("The travel waits for the destroy"), FEasySessionTestAccess::IsWaitingToTravelToGameDefaultMap(*Subsystem));
			CurrentTest->TestFalse(TEXT("No travel has started before the destroy"), FEasySessionTestAccess::IsTraveling(*Subsystem));

			State->Phase = 1;
			return false;
		}

		default:
		{
			if (FEasySessionTestAccess::IsWaitingToTravelToGameDefaultMap(*Subsystem))
			{
				return false;
			}

			CurrentTest->TestFalse(TEXT("The lost session is destroyed"), Subsystem->IsInSession());
			CurrentTest->TestTrue(TEXT("The menu travel starts after the destroy"), FEasySessionTestAccess::IsTraveling(*Subsystem));

			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * With the EasySession online session, a client's engine-reported disconnect destroys the lost session before the menu travel starts.
 * Two reports of the same disconnect queue one destroy and wait once, so the menu loads once.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionEngineDisconnectTest, "EasySession.OnlineSession.EngineDisconnectTravelsAfterCleanup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionEngineDisconnectTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionOnlineSessionTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	FEasySessionHostParams Params;
	Params.SessionDisplayName = TEXT("EasySession Engine Disconnect Test");
	Params.bIsLANMatch = true;
	Params.InitialMapName = EasySessionTest::SessionMapName;
	Subsystem->CreateSession(Params, FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->CreateResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForEngineDisconnect(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
