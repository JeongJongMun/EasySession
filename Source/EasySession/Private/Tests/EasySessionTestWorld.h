// Copyright (c) 2026 Langerak.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionGameInstance.h"
#include "EasySessionTestAccess.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"

namespace EasySessionTest
{
	/**
	 * The InitialMapName the tests host with.
	 * CreateSession refuses host params without one, and no test travels there, so the map does not have to exist.
	 */
	inline const TCHAR* const SessionMapName = TEXT("/Game/EasySessionTestMap");

	/** Create a test game instance of the class EasySession needs, so its requests are not refused with NoEasySessionGameInstance. */
	inline UEasySessionGameInstance* NewGameInstance()
	{
		return NewObject<UEasySessionGameInstance>(GEngine);
	}

	/**
	 * Initialize a test game instance and disable the host's travel in its subsystem.
	 * A headless world has no player controller, so the travel to InitialMapName would always fail.
	 * A test that needs the state actor or the reservation beacon calls FEasySessionTestAccess::ArriveInSessionMap after the create.
	 */
	template <typename GameInstancePtrType>
	void InitializeGameInstance(const GameInstancePtrType& GameInstance)
	{
		GameInstance->InitializeStandalone();

		if (UEasySessionSubsystem* Subsystem = GameInstance->template GetSubsystem<UEasySessionSubsystem>())
		{
			FEasySessionTestAccess::SkipHostTravel(*Subsystem);
		}
	}

	/**
	 * Shut a test game instance down and destroy the world it brought with it.
	 *
	 * UGameInstance::InitializeStandalone creates a world context and a dummy world ("/Temp/Untitled_N") to run in.
	 * Shutdown() only clears the instance's own pointer to them, so the world stays alive and still initialized.
	 * Its world subsystems (WorldPartition, Water, ...) then outlast the test, and when the editor finally exits they are destroyed while still initialized:
	 *
	 *   Ensure condition failed: !bInitialized  [WorldSubsystem.cpp:118]
	 *   Tickable subsystem WorldPartitionSubsystem /Temp/Untitled_1 was destroyed
	 *   while still initialized! Check for missing Super::Deinitialize call
	 *
	 * That is followed by an access violation on shutdown, so a test that starts an instance must undo both halves.
	 * The order mirrors the engine's own test helper, FTestWorldInstance::Teardown in NetTestHelpers.cpp.
	 * Drop the net driver, destroy the world, then remove its context.
	 *
	 * The sessions and the search the test left in the online subsystem are released too.
	 * Every test in the process shares one online subsystem, and Shutdown leaves both in it.
	 * A test that ends early would otherwise fail the next test's create with SessionAlreadyExists, or its search with SearchFailure.
	 */
	inline void DestroyGameInstance(UGameInstance* GameInstance)
	{
		if (GameInstance == nullptr)
		{
			return;
		}

		// Capture the world first. Shutdown clears the instance's world context.
		UWorld* World = GameInstance->GetWorld();
		const IOnlineSessionPtr Sessions = Online::GetSessionInterface(World);

		// Read the search before Shutdown destroys the request that holds it.
		const UEasySessionSubsystem* Subsystem = GameInstance->GetSubsystem<UEasySessionSubsystem>();
		const TSharedPtr<FOnlineSessionSearch> Search = Subsystem != nullptr ? FEasySessionTestAccess::GetActiveSearch(*Subsystem) : nullptr;
		const bool bSearchRunning = Search.IsValid() && Search->SearchState == EOnlineAsyncTaskState::InProgress;

		GameInstance->Shutdown();

		// Released after Shutdown, so the completions they trigger reach no request of the destroyed subsystem.
		if (Sessions.IsValid())
		{
			if (bSearchRunning)
			{
				Sessions->CancelFindSessions();
			}

			for (const FName SessionName : { NAME_GameSession, NAME_PartySession })
			{
				if (Sessions->GetNamedSession(SessionName) != nullptr)
				{
					Sessions->DestroySession(SessionName);
				}
			}
		}

		if (World == nullptr || GEngine == nullptr)
		{
			return;
		}

		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(/*bInformEngineOfWorld=*/ true);
		GEngine->DestroyWorldContext(World);
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
