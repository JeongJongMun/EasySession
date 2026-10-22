// Copyright (c) 2026 Langerak.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestEventListener.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionSettingsPropagationTest
{
	/** Maximum time to wait for each queued request before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	/** Which step of the create/update/leave sequence the latent command is waiting on. */
	enum class EStep : uint8
	{
		AwaitingCreate,
		AwaitingUpdate,
		AwaitingDestroy,
		AwaitingRecreate,
		AwaitingSecondDestroy,
		Done
	};

	struct FTestState
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UEasySessionTestEventListener> Listener;
		EStep Step = EStep::AwaitingCreate;
		TOptional<EEasySessionResult> PendingResult;

		/** The settings the apply test sends as the host's, kept to send the same ones again in a new session. */
		FEasySessionReplicatedSettings Payload;

		double StartTime = 0.0;
	};

	static FEasySessionHostParams MakeParams(const FString& DisplayName)
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = DisplayName;
		Params.MaxPlayers = 4;
		Params.bUseJoinCode = true;
		Params.bIsLANMatch = true;
		Params.InitialMapName = EasySessionTest::SessionMapName;
		return Params;
	}

	/** Take the result the last step produced, leaving it empty for the next one. */
	static EEasySessionResult ConsumeResult(FTestState& State)
	{
		const EEasySessionResult Result = State.PendingResult.GetValue();
		State.PendingResult.Reset();
		State.StartTime = FPlatformTime::Seconds();
		return Result;
	}
}

// ---------------------------------------------------------------------------
// Push path: a successful update reaches the state actor's payload.
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
			CurrentTest->AddError(TEXT("Timed out waiting for a session request to complete."));
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

			// Arriving in the session's map spawns the state actor with a first payload.
			// A player who joins before any update still receives the settings.
			FEasySessionTestAccess::ArriveInSessionMap(*Subsystem);
			const FEasySessionReplicatedSettings AfterCreate = FEasySessionTestAccess::GetStateActorReplicatedSettings(*Subsystem);
			CurrentTest->TestTrue(TEXT("Create wrote a payload into the state actor"), AfterCreate.bValid);
			CurrentTest->TestEqual(TEXT("Payload carries the created display name"), AfterCreate.SessionDisplayName, TEXT("Settings Push Before"));
			CurrentTest->TestEqual(TEXT("Payload carries the created cap"), AfterCreate.MaxPlayers, 4);
			CurrentTest->TestEqual(TEXT("Payload carries the join code, so members can share it"),
				AfterCreate.JoinCode, Subsystem->GetJoinCode());
			CurrentTest->TestFalse(TEXT("The created session advertised a join code"), AfterCreate.JoinCode.IsEmpty());

			FEasySessionSettings Updated = Subsystem->GetSessionSettings();
			Updated.SessionDisplayName = TEXT("Settings Push After");
			Updated.MaxPlayers = 6;
			Updated.bHidden = true;
			Updated.CustomSettings.Add(TEXT("GameMode"), FEasySessionSettingValue::MakeString(TEXT("Conquest")));
			Updated.CustomSettings.Add(TEXT("Rounds"), FEasySessionSettingValue::MakeInt(5));
			State->Step = EStep::AwaitingUpdate;
			Subsystem->UpdateSession(Updated, FEasySessionCompleteDelegate::CreateLambda(
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

			const FEasySessionReplicatedSetting* Custom = Payload.CustomSettings.FindByPredicate(
				[](const FEasySessionReplicatedSetting& Setting) { return Setting.Key == TEXT("GameMode"); });
			if (CurrentTest->TestNotNull(TEXT("Payload carries the custom setting"), Custom))
			{
				CurrentTest->TestTrue(TEXT("Custom setting value survived"), Custom->Value == FEasySessionSettingValue::MakeString(TEXT("Conquest")));
			}

			const FEasySessionReplicatedSetting* Rounds = Payload.CustomSettings.FindByPredicate(
				[](const FEasySessionReplicatedSetting& Setting) { return Setting.Key == TEXT("Rounds"); });
			if (CurrentTest->TestNotNull(TEXT("Payload carries the number setting"), Rounds))
			{
				CurrentTest->TestTrue(TEXT("The number setting stays a number"), Rounds->Value == FEasySessionSettingValue::MakeInt(5));
			}

			// The plugin's own keys stay out of the custom list.
			// They are either dedicated payload fields or internal, like the reservations key.
			const bool bLeakedReservedKey = Payload.CustomSettings.ContainsByPredicate(
				[](const FEasySessionReplicatedSetting& Setting) { return EasySession::IsReservedSettingKey(FName(*Setting.Key)); });
			CurrentTest->TestFalse(TEXT("No reserved key leaked into the custom list"), bLeakedReservedKey);

			State->Step = EStep::AwaitingDestroy;
			Subsystem->LeaveSession(EEasySessionType::GameSession, FEasySessionCompleteDelegate::CreateLambda(
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
	State->GameInstance = TStrongObjectPtr<UGameInstance>(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	Subsystem->CreateSession(MakeParams(TEXT("Settings Push Before")), FEasySessionCompleteDelegate::CreateLambda(
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
			CurrentTest->AddError(TEXT("Timed out waiting for a session request to complete."));
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

			// A joined client holds a local session copy but does not host it.
			// That state is set directly here, because a headless test has no second process.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false);

			FEasySessionReplicatedSettings& Payload = State->Payload;
			Payload.SessionDisplayName = TEXT("Renamed By Host");
			Payload.JoinCode = TEXT("XYZ234");
			Payload.MaxPlayers = 8;
			Payload.bHidden = true;
			Payload.bPasswordProtected = true;
			FEasySessionReplicatedSetting Custom;
			Custom.Key = TEXT("GameMode");
			Custom.Value = FEasySessionSettingValue::MakeString(TEXT("Conquest"));
			Payload.CustomSettings.Add(Custom);
			FEasySessionReplicatedSetting Rounds;
			Rounds.Key = TEXT("Rounds");
			Rounds.Value = FEasySessionSettingValue::MakeInt(5);
			Payload.CustomSettings.Add(Rounds);

			// A default payload means the host wrote nothing yet. It must change nothing.
			FEasySessionReplicatedSettings NotWritten = Payload;
			NotWritten.bValid = false;
			FEasySessionTestAccess::DriveReplicatedSessionSettings(*Subsystem, NotWritten);
			CurrentTest->TestEqual(TEXT("An unwritten payload changed nothing"),
				Subsystem->GetDisplayName(), TEXT("Settings Apply Before"));
			CurrentTest->TestEqual(TEXT("An unwritten payload fired no event"), State->Listener->SettingsChangedBroadcasts, 0);

			Payload.bValid = true;
			FEasySessionTestAccess::DriveReplicatedSessionSettings(*Subsystem, Payload);

			CurrentTest->TestEqual(TEXT("The client's display name follows the host"),
				Subsystem->GetDisplayName(), TEXT("Renamed By Host"));
			CurrentTest->TestEqual(TEXT("The client's cap follows the host"), Subsystem->GetMaxPlayers(), 8);
			CurrentTest->TestEqual(TEXT("The client's join code follows the host"),
				Subsystem->GetJoinCode(), TEXT("XYZ234"));
			CurrentTest->TestEqual(TEXT("The client's hidden flag follows the host"),
				FEasySessionTestAccess::GetAdvertisedSettingInt(*Subsystem, EasySession::SettingKey_Hidden), 1);
			const FEasySessionSettings ClientSettings = Subsystem->GetSessionSettings();
			CurrentTest->TestTrue(TEXT("The client's text setting follows the host"), ClientSettings.CustomSettings.FindRef(TEXT("GameMode")) == FEasySessionSettingValue::MakeString(TEXT("Conquest")));
			CurrentTest->TestTrue(TEXT("The client reads the number setting as a number"), ClientSettings.CustomSettings.FindRef(TEXT("Rounds")) == FEasySessionSettingValue::MakeInt(5));
			CurrentTest->TestEqual(TEXT("The settings changed event fired once"), State->Listener->SettingsChangedBroadcasts, 1);

			// The same payload can arrive twice, through PostNetInit and the OnRep, and it must apply once.
			FEasySessionTestAccess::DriveReplicatedSessionSettings(*Subsystem, Payload);
			CurrentTest->TestEqual(TEXT("A repeated payload fired no second event"), State->Listener->SettingsChangedBroadcasts, 1);

			// Make this game the host again so the cleanup runs the host's destroy path.
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, true);
			State->Step = EStep::AwaitingDestroy;
			Subsystem->LeaveSession(EEasySessionType::GameSession, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->PendingResult = Result;
				}));
			return false;
		}

		case EStep::AwaitingDestroy:
		{
			CurrentTest->TestEqual(TEXT("Session destroyed"), ConsumeResult(*State), EEasySessionResult::Success);

			// A rejoined session can carry the same settings as the last one, and they must still apply.
			State->Step = EStep::AwaitingRecreate;
			Subsystem->CreateSession(MakeParams(TEXT("Settings Apply Before")), FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->PendingResult = Result;
				}));
			return false;
		}

		case EStep::AwaitingRecreate:
		{
			CurrentTest->TestEqual(TEXT("Session created again"), ConsumeResult(*State), EEasySessionResult::Success);
			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, false);

			FEasySessionTestAccess::DriveReplicatedSessionSettings(*Subsystem, State->Payload);
			CurrentTest->TestEqual(TEXT("The same settings in a new session fired the event again"), State->Listener->SettingsChangedBroadcasts, 2);
			CurrentTest->TestEqual(TEXT("The new session copy follows the host"), Subsystem->GetDisplayName(), TEXT("Renamed By Host"));

			FEasySessionTestAccess::SetCreatedActiveSession(*Subsystem, true);
			State->Step = EStep::AwaitingSecondDestroy;
			Subsystem->LeaveSession(EEasySessionType::GameSession, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->PendingResult = Result;
				}));
			return false;
		}

		case EStep::AwaitingSecondDestroy:
		{
			CurrentTest->TestEqual(TEXT("Second session destroyed"), ConsumeResult(*State), EEasySessionResult::Success);
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
	State->GameInstance = TStrongObjectPtr<UGameInstance>(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(State->GameInstance);

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	State->Listener = TStrongObjectPtr<UEasySessionTestEventListener>(NewObject<UEasySessionTestEventListener>(State->GameInstance.Get()));
	Subsystem->OnSessionSettingsChanged.AddDynamic(State->Listener.Get(), &UEasySessionTestEventListener::HandleSettingsChanged);

	Subsystem->CreateSession(MakeParams(TEXT("Settings Apply Before")), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString&)
		{
			State->PendingResult = Result;
		}));

	State->StartTime = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForSettingsApply(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
