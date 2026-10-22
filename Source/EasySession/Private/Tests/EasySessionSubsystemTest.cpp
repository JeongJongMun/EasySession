// Copyright (c) 2026 Langerak.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionSubsystem.h"
#include "EasySessionTestWorld.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Outside a session the world's game state still lists the local player.
 * The player count and the player list describe the session, so they report nobody there, as Max Players already does.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEasySessionNoPlayersOutsideSessionTest, "EasySession.Subsystem.NoPlayersOutsideASession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FEasySessionNoPlayersOutsideSessionTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> GameInstance(EasySessionTest::NewGameInstance());
	EasySessionTest::InitializeGameInstance(GameInstance);

	UEasySessionSubsystem* Subsystem = GameInstance->GetSubsystem<UEasySessionSubsystem>();
	UWorld* World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("EasySessionSubsystem is available"), Subsystem) || !TestNotNull(TEXT("The game instance has a world"), World))
	{
		EasySessionTest::DestroyGameInstance(GameInstance.Get());
		return false;
	}

	// A menu map: a game state that lists the local player's player state.
	// A player state adds itself in PostInitializeComponents, which a world that never began play does not run, so the test adds it.
	AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
	World->SetGameState(GameState);
	GameState->AddPlayerState(World->SpawnActor<APlayerState>());
	TestEqual(TEXT("The game state lists the local player"), GameState->PlayerArray.Num(), 1);

	TestFalse(TEXT("There is no session"), Subsystem->IsInSession());
	TestEqual(TEXT("The player count is 0 outside a session"), Subsystem->GetPlayerCount(), 0);
	TestEqual(TEXT("The player list is empty outside a session"), Subsystem->GetSessionPlayerInfos().Num(), 0);

	EasySessionTest::DestroyGameInstance(GameInstance.Get());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
