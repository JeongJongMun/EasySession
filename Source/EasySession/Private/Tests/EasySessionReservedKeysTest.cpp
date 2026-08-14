// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionStatics.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTestAccess.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Online/OnlineSessionNames.h"
#include "UObject/StrongObjectPtr.h"

namespace EasySessionReservedKeysTest
{
	/** Maximum time to wait for each queued operation before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	/** Which step of the create/update/destroy sequence the latent command is waiting on. */
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
		EStep Step = EStep::AwaitingCreate;
		TOptional<EEasySessionResult> PendingResult;
		double StartTime = 0.0;
	};

	static FEasySessionHostParams MakeParams()
	{
		FEasySessionHostParams Params;
		Params.SessionDisplayName = TEXT("EasySession Reserved Keys Test");
		Params.bIsLANMatch = true;
		Params.bStartListening = false;
		Params.CustomSettings.Add(TEXT("GameMode"), TEXT("CTF"));
		return Params;
	}
}

/**
 * Walks create, update and destroy, checking the reserved keys between the steps.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionRunReservedKeySteps, TSharedPtr<EasySessionReservedKeysTest::FTestState>, State);
bool FEasySessionRunReservedKeySteps::Update()
{
	using namespace EasySessionReservedKeysTest;

	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();

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

	const EEasySessionResult Result = State->PendingResult.GetValue();
	State->PendingResult.Reset();

	switch (State->Step)
	{
	case EStep::AwaitingCreate:
	{
		if (!CurrentTest->TestEqual(TEXT("Create succeeded"), Result, EEasySessionResult::Success))
		{
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}

		CurrentTest->TestEqual(TEXT("Join approval is advertised as a number"),
			FEasySessionTestAccess::GetAdvertisedSettingType(*Subsystem, EasySession::SettingKey_JoinApproval), EOnlineKeyValuePairDataType::Int32);
		CurrentTest->TestEqual(TEXT("Beacon port is advertised as a number"),
			FEasySessionTestAccess::GetAdvertisedSettingType(*Subsystem, SETTING_BEACONPORT), EOnlineKeyValuePairDataType::Int32);

		// What a Blueprint gets from Get Easy Session Host Params. The plugin's own keys
		// must not be in there, or handing this struct straight back to Update rewrites them.
		const FEasySessionHostParams ReadBack = Subsystem->GetEasySessionHostParams();
		CurrentTest->TestFalse(TEXT("Join approval is not exposed as a custom setting"),
			ReadBack.CustomSettings.Contains(EasySession::SettingKey_JoinApproval.ToString()));
		CurrentTest->TestFalse(TEXT("Beacon port is not exposed as a custom setting"),
			ReadBack.CustomSettings.Contains(SETTING_BEACONPORT.ToString()));
		CurrentTest->TestTrue(TEXT("The game's own custom setting survives the read"),
			ReadBack.CustomSettings.Contains(TEXT("GameMode")));

		// The round trip a game is told to make: read, change one field, hand it back.
		FEasySessionHostParams Updated = ReadBack;
		Updated.MaxPlayers = 8;

		State->Step = EStep::AwaitingUpdate;
		State->StartTime = FPlatformTime::Seconds();
		TSharedPtr<FTestState> UpdateState = State;
		Subsystem->UpdateEasySession(Updated, FEasySessionCompleteDelegate::CreateLambda(
			[UpdateState](EEasySessionResult UpdateResult, const FString& /*ErrorMessage*/)
			{
				UpdateState->PendingResult = UpdateResult;
			}));
		return false;
	}

	case EStep::AwaitingUpdate:
	{
		CurrentTest->TestEqual(TEXT("Update succeeded"), Result, EEasySessionResult::Success);

		CurrentTest->TestEqual(TEXT("Join approval is still a number after the round trip"),
			FEasySessionTestAccess::GetAdvertisedSettingType(*Subsystem, EasySession::SettingKey_JoinApproval), EOnlineKeyValuePairDataType::Int32);
		CurrentTest->TestEqual(TEXT("Beacon port is still a number after the round trip"),
			FEasySessionTestAccess::GetAdvertisedSettingType(*Subsystem, SETTING_BEACONPORT), EOnlineKeyValuePairDataType::Int32);

		// The values matter as much as the types: a string rewrite leaves the key in place
		// and every reader sees zero, so the host stops standing up its approval beacon.
		CurrentTest->TestEqual(TEXT("Join approval still reads as enabled"),
			FEasySessionTestAccess::GetAdvertisedSettingInt(*Subsystem, EasySession::SettingKey_JoinApproval), 1);
		CurrentTest->TestEqual(TEXT("Beacon port still reads as the configured port"),
			FEasySessionTestAccess::GetAdvertisedSettingInt(*Subsystem, SETTING_BEACONPORT), EasySession::GetJoinApprovalBeaconPort());

		CurrentTest->TestEqual(TEXT("Max players took the update"), Subsystem->GetSessionMaxPlayers(), 8);

		State->Step = EStep::AwaitingDestroy;
		State->StartTime = FPlatformTime::Seconds();
		TSharedPtr<FTestState> DestroyState = State;
		Subsystem->DestroyEasySession(FEasySessionCompleteDelegate::CreateLambda(
			[DestroyState](EEasySessionResult DestroyResult, const FString& /*ErrorMessage*/)
			{
				DestroyState->PendingResult = DestroyResult;
			}));
		return false;
	}

	case EStep::AwaitingDestroy:
	default:
		CurrentTest->TestEqual(TEXT("Destroy succeeded"), Result, EEasySessionResult::Success);
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return true;
	}
}

/**
 * Reading the host params and handing them back to Update leaves the plugin's own keys alone.
 *
 * The keys are advertised as numbers and Custom Settings is a string map, so a key that leaks
 * into that map comes back as a string. The key stays present, so every reader gets zero
 * instead of a missing key, and the host quietly stops answering join approval.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionReservedKeysTest, "EasySession.Subsystem.HostParamsRoundTripKeepsReservedKeys", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionReservedKeysTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionReservedKeysTest;

	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>(GEngine));
	State->GameInstance->InitializeStandalone();

	UEasySessionSubsystem* Subsystem = State->GameInstance->GetSubsystem<UEasySessionSubsystem>();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem))
	{
		EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
		return false;
	}

	State->StartTime = FPlatformTime::Seconds();
	Subsystem->CreateEasySession(MakeParams(), FEasySessionCompleteDelegate::CreateLambda(
		[State](EEasySessionResult Result, const FString& /*ErrorMessage*/)
		{
			State->PendingResult = Result;
		}));

	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionRunReservedKeySteps(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
