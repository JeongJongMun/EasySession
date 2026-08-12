// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace EasySessionTest
{
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
	 */
	inline void DestroyGameInstance(UGameInstance* GameInstance)
	{
		if (GameInstance == nullptr)
		{
			return;
		}

		// Capture the world first - Shutdown clears the instance's world context.
		UWorld* World = GameInstance->GetWorld();
		GameInstance->Shutdown();

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
