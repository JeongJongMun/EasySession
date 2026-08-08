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

namespace EasySessionPasswordUpdateTest
{
	/** Maximum time to wait for each queued operation before failing the test. */
	static constexpr double TimeoutSeconds = 20.0;

	/** Which step of the create/lock/unlock sequence the latent command is waiting on. */
	enum class EStep : uint8
	{
		AwaitingCreate,
		AwaitingLock,
		AwaitingUnlock,
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
		Params.SessionDisplayName = TEXT("EasySession Password Update");
		Params.MaxPlayers = 6;
		Params.bIsLANMatch = true;
		Params.bStartListening = false;
		Params.bAllowJoinInProgress = false;

		// Whitespace only, so the session starts open. What it advertises is decided
		// separately from what it enforces, and both have to agree that this is not
		// a password.
		Params.Password = TEXT("   ");
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

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEasySessionWaitForPasswordUpdate, TSharedPtr<EasySessionPasswordUpdateTest::FTestState>, State);
bool FEasySessionWaitForPasswordUpdate::Update()
{
	using namespace EasySessionPasswordUpdateTest;

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

			CurrentTest->TestFalse(TEXT("An open session does not advertise a password"), FEasySessionTestAccess::GetAdvertisedPasswordProtected(*Subsystem));
			CurrentTest->TestEqual(TEXT("An open session enforces no password"), FEasySessionTestAccess::GetEnforcedSessionPassword(*Subsystem), FString());

			// Reading the session back has to return what it was created with, or the
			// read-modify-write below would quietly change fields nobody touched.
			const FEasySessionHostParams ReadBack = Subsystem->GetEasySessionHostParams();
			CurrentTest->TestEqual(TEXT("Read back the display name"), ReadBack.SessionDisplayName, MakeParams().SessionDisplayName);
			CurrentTest->TestEqual(TEXT("Read back the player limit"), ReadBack.MaxPlayers, 6);
			CurrentTest->TestFalse(TEXT("Read back join in progress"), ReadBack.bAllowJoinInProgress);
			CurrentTest->TestEqual(TEXT("Read back the empty password"), ReadBack.Password, FString());

			// Lock the room by changing only the password.
			FEasySessionHostParams Locked = ReadBack;
			Locked.Password = TEXT("1234");
			State->Step = EStep::AwaitingLock;
			Subsystem->UpdateEasySession(Locked, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->PendingResult = Result;
				}));
			return false;
		}

		case EStep::AwaitingLock:
		{
			CurrentTest->TestEqual(TEXT("Locking the session succeeded"), ConsumeResult(*State), EEasySessionResult::Success);

			// The advertised flag and the password arriving players are checked against
			// are set from two different places. Either one alone would be a lie.
			CurrentTest->TestTrue(TEXT("A locked session advertises that it is protected"), FEasySessionTestAccess::GetAdvertisedPasswordProtected(*Subsystem));
			CurrentTest->TestEqual(TEXT("A locked session enforces the new password"), FEasySessionTestAccess::GetEnforcedSessionPassword(*Subsystem), FString(TEXT("1234")));

			// Fields the caller did not touch have to survive the update.
			const FEasySessionHostParams AfterLock = Subsystem->GetEasySessionHostParams();
			CurrentTest->TestEqual(TEXT("Locking left the display name alone"), AfterLock.SessionDisplayName, MakeParams().SessionDisplayName);
			CurrentTest->TestEqual(TEXT("Locking left the player limit alone"), AfterLock.MaxPlayers, 6);
			CurrentTest->TestFalse(TEXT("Locking left join in progress alone"), AfterLock.bAllowJoinInProgress);
			CurrentTest->TestEqual(TEXT("Locking is visible when reading back"), AfterLock.Password, FString(TEXT("1234")));

			// Unlock it again. An empty password has to be able to undo a lock, which
			// is why the advertised flag is written for both states instead of only
			// being added when set.
			FEasySessionHostParams Unlocked = AfterLock;
			Unlocked.Password = FString();
			State->Step = EStep::AwaitingUnlock;
			Subsystem->UpdateEasySession(Unlocked, FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->PendingResult = Result;
				}));
			return false;
		}

		case EStep::AwaitingUnlock:
		{
			CurrentTest->TestEqual(TEXT("Unlocking the session succeeded"), ConsumeResult(*State), EEasySessionResult::Success);

			CurrentTest->TestFalse(TEXT("An unlocked session stops advertising a password"), FEasySessionTestAccess::GetAdvertisedPasswordProtected(*Subsystem));
			CurrentTest->TestEqual(TEXT("An unlocked session enforces no password"), FEasySessionTestAccess::GetEnforcedSessionPassword(*Subsystem), FString());

			// Sessions live in the online service per process, so one left behind fails
			// the next test's create.
			State->Step = EStep::AwaitingDestroy;
			Subsystem->DestroyEasySession(FEasySessionCompleteDelegate::CreateLambda(
				[Shared](EEasySessionResult Result, const FString&)
				{
					Shared->PendingResult = Result;
				}));
			return false;
		}

		case EStep::AwaitingDestroy:
		default:
		{
			CurrentTest->TestEqual(TEXT("Session destroyed"), ConsumeResult(*State), EEasySessionResult::Success);
			State->Step = EStep::Done;
			EasySessionTest::DestroyGameInstance(State->GameInstance.Get());
			return true;
		}
	}
}

/**
 * A host must be able to lock and unlock a running session, and both halves of a
 * password have to move together: the flag searching players see, and the value
 * arriving players are checked against. Update used to write neither, reporting
 * Success while the room stayed open.
 *
 * The read-modify-write assertions guard the other half of that contract - Update
 * applies every field it is given, so a caller needs to be able to ask what the
 * session is running with rather than building params from defaults.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionPasswordUpdateTest, "EasySession.Subsystem.UpdateChangesThePassword", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionPasswordUpdateTest::RunTest(const FString& Parameters)
{
	using namespace EasySessionPasswordUpdateTest;

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
	ADD_LATENT_AUTOMATION_COMMAND(FEasySessionWaitForPasswordUpdate(State));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
