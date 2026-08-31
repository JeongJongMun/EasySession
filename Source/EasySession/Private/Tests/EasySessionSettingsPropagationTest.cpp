// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySession.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionSettingsPropagationTest
{
	/** Maximum time to wait for each queued operation before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	/** Which step of the create/update/leave sequence the latent command is waiting on. */
	enum class EStep : uint8
	{
		AwaitingCreate,
		AwaitingUpdate,
		AwaitingDestroy,
		Done
	};

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;
		EStep Step = EStep::AwaitingCreate;
		TOptional<EEasySessionResult> PendingResult;
		double StartTime = 0.0;
	};

	static FEasySessionHostParams MakeParams(const FString& DisplayName)
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = DisplayName;
		Params.MaxPlayers = 4;
		Params.bUseJoinCode = true;
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

// ---------------------------------------------------------------------------
// Push path: a successful update lands in the state actor's payload.
// ---------------------------------------------------------------------------

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForSettingsPush, TSharedPtr<EasySessionSettingsPropagationTest::FTestState>, State);
bool FEasySessionWaitForSettingsPush::Update()
{
	using namespace EasySessionSettingsPropagationTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
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

			// Creation spawns the state actor and pushes a first payload, so a
			// player who joins before any update still receives the settings.
			const FEasySessionReplicatedSettings AfterCreate = FEasySessionTestAccess::GetStateActorReplicatedSettings(*Subsystem);
			CurrentTest->TestTrue(TEXT("Create wrote a payload into the state actor"), AfterCreate.bValid);
			CurrentTest->TestEqual(TEXT("Payload carries the created display name"), AfterCreate.SessionDisplayName, TEXT("Settings Push Before"));
			CurrentTest->TestEqual(TEXT("Payload carries the created cap"), AfterCreate.MaxPlayers, 4);
			CurrentTest->TestEqual(TEXT("Payload carries the join code, so members can share it"),
				AfterCreate.JoinCode, Subsystem->GetSessionJoinCode());
			CurrentTest->TestFalse(TEXT("The created session advertised a join code"), AfterCreate.JoinCode.IsEmpty());

			FEasySessionSettings Updated = Subsystem->GetSessionSettings();
			Updated.SessionDisplayName = TEXT("Settings Push After");
			Updated.MaxPlayers = 6;
			Updated.bHidden = true;
			Updated.Region = EEasySessionRegion::SouthAmerica;
			Updated.CustomSettings.Add(TEXT("GameMode"), TEXT("Conquest"));
			State->Step = EStep::AwaitingUpdate;
			Subsystem->UpdateEasySession(Updated, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->PendingResult = Result;
				}));
			return false;
		}

		case EStep::AwaitingUpdate:
		{
			CurrentTest->TestEqual(TEXT("Update succeeded"), ConsumeResult(*State), EEasySessionResult::Success);

			const FEasySessionReplicatedSettings Payload = FEasySessionTestAccess::GetStateActorReplicatedSettings(*Subsystem);
			CurrentTest->TestTrue(TEXT("Update wrote a payload into the state actor"), Payload.bValid);
			CurrentTest->TestEqual(TEXT("Payload carries the updated display name"), Payload.SessionDisplayName, TEXT("Settings Push After"));
			CurrentTest->TestEqual(TEXT("Payload carries the updated cap"), Payload.MaxPlayers, 6);
			CurrentTest->TestTrue(TEXT("Payload carries the hidden flag"), Payload.bHidden);
			CurrentTest->TestEqual(TEXT("Payload carries the region"), Payload.Region, EEasySessionRegion::SouthAmerica);

			const FEasySessionReplicatedSetting* Custom = Payload.CustomSettings.FindByPredicate(
				[](const FEasySessionReplicatedSetting& Setting) { return Setting.Key == TEXT("GameMode"); });
			if (CurrentTest->TestNotNull(TEXT("Payload carries the custom setting"), Custom))
			{
				CurrentTest->TestEqual(TEXT("Custom setting value survived"), Custom->Value, TEXT("Conquest"));
			}

			// The plugin's own keys stay out of the custom list - they are either
			// dedicated payload fields or internal, like the join approval flag.
			const bool bLeakedReservedKey = Payload.CustomSettings.ContainsByPredicate(
				[](const FEasySessionReplicatedSetting& Setting) { return EasySession::IsReservedSettingKey(FName(*Setting.Key)); });
			CurrentTest->TestFalse(TEXT("No reserved key leaked into the custom list"), bLeakedReservedKey);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionSettingsPushTest,
	"EasySession.Subsystem.UpdatePushesSettingsToTheStateActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionSettingsPushTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionSettingsPropagationTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	Subsystem->CreateEasySession(MakeParams(TEXT("Settings Push Before")), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->PendingResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForSettingsPush(State));
	return true;
}

// ---------------------------------------------------------------------------
// Apply path: a replicated payload patches the local session copy on a client.
// ---------------------------------------------------------------------------

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForSettingsApply, TSharedPtr<EasySessionSettingsPropagationTest::FTestState>, State);
bool FEasySessionWaitForSettingsApply::Update()
{
	using namespace EasySessionSettingsPropagationTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
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

			// A joined client holds a local session copy but no authority - fake
			// that shape here, because a headless test has no second process.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false);

			FEasySessionReplicatedSettings Payload;
			Payload.SessionDisplayName = TEXT("Renamed By Host");
			Payload.JoinCode = TEXT("XYZ234");
			Payload.MaxPlayers = 8;
			Payload.bHidden = true;
			Payload.bPasswordProtected = true;
			Payload.Region = EEasySessionRegion::SouthAmerica;
			FEasySessionReplicatedSetting Custom;
			Custom.Key = TEXT("GameMode");
			Custom.Value = TEXT("Conquest");
			Payload.CustomSettings.Add(Custom);

			// A default payload means the host wrote nothing yet - it must change nothing.
			FEasySessionReplicatedSettings NotWritten = Payload;
			NotWritten.bValid = false;
			FEasySessionTestAccess::DriveReplicatedSessionSettings(*Subsystem, NotWritten);
			CurrentTest->TestEqual(TEXT("An unwritten payload changed nothing"),
				Subsystem->GetSessionDisplayName(), TEXT("Settings Apply Before"));
			CurrentTest->TestEqual(TEXT("An unwritten payload fired no event"), State->Listener->SettingsChangedBroadcasts, 0);

			Payload.bValid = true;
			FEasySessionTestAccess::DriveReplicatedSessionSettings(*Subsystem, Payload);

			CurrentTest->TestEqual(TEXT("The client's display name follows the host"),
				Subsystem->GetSessionDisplayName(), TEXT("Renamed By Host"));
			CurrentTest->TestEqual(TEXT("The client's cap follows the host"), Subsystem->GetSessionMaxPlayers(), 8);
			CurrentTest->TestEqual(TEXT("The client's join code follows the host"),
				Subsystem->GetSessionJoinCode(), TEXT("XYZ234"));
			CurrentTest->TestEqual(TEXT("The client's hidden flag follows the host"),
				FEasySessionTestAccess::GetAdvertisedSettingInt(*Subsystem, EasySession::SettingKey_Hidden), 1);
			CurrentTest->TestEqual(TEXT("The client's region follows the host"),
				FEasySessionTestAccess::GetAdvertisedSettingInt(*Subsystem, EasySession::SettingKey_Region),
				static_cast<int32>(EEasySessionRegion::SouthAmerica));
			CurrentTest->TestEqual(TEXT("The settings changed event fired once"), State->Listener->SettingsChangedBroadcasts, 1);

			// The same payload can arrive twice, through PostNetInit and the OnRep - it must apply once.
			FEasySessionTestAccess::DriveReplicatedSessionSettings(*Subsystem, Payload);
			CurrentTest->TestEqual(TEXT("A repeated payload fired no second event"), State->Listener->SettingsChangedBroadcasts, 1);

			// Hand authority back so teardown runs the host's destroy path.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, true);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionSettingsApplyTest,
	"EasySession.Subsystem.ClientAppliesReplicatedSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionSettingsApplyTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionSettingsPropagationTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	State->Listener = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>(State->GameInstance.Get()));
	Subsystem->OnSessionSettingsChanged.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleSettingsChanged);

	Subsystem->CreateEasySession(MakeParams(TEXT("Settings Apply Before")), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->PendingResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForSettingsApply(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
