// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionTravel.h"

#include "EasySession.h"
#include "EasySessionAddress.h"
#include "EasySessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectGlobals.h"

FEasySessionTravel::FEasySessionTravel(UEasySessionSubsystem& InOwner)
	: Owner(InOwner)
{
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FEasySessionTravel::HandlePostLoadMap);
}

FEasySessionTravel::~FEasySessionTravel()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	PostLoadMapHandle.Reset();
}

void FEasySessionTravel::TravelToOwnSession(const FEasySessionHostParams& HostParams)
{
	if (bSkipHostTravel)
	{
		return;
	}

	FString TravelURL = HostParams.InitialMapName.TrimStartAndEnd();
	if (!EasySessionAddress::HasListenOption(TravelURL))
	{
		TravelURL += TEXT("?listen");
	}
	AppendTravelOptions(TravelURL, HostParams.AdditionalTravelOptions);
	// The engine reads this into AGameSession::MaxPlayers, so its "Server full" refusal matches the advertised capacity.
	EasySessionAddress::AppendMaxPlayersOption(TravelURL, HostParams.MaxPlayers);
	Owner.OnModifyServerTravelURL.Broadcast(TravelURL);

	UE_LOG(LogEasySession, Log, TEXT("Traveling to session map '%s'"), *TravelURL);

	// A client travel loads the map in a new world, so ?listen opens the listen server and players connected before the session are disconnected.
	APlayerController* PlayerController = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		UE_LOG(LogEasySession, Warning, TEXT("No local player controller to travel with. Travel to '%s' aborted."), *TravelURL);
		Owner.OnSessionFailure.Broadcast(FString::Printf(TEXT("Travel to '%s' failed."), *TravelURL));
		return;
	}

	PlayerController->ClientTravel(TravelURL, TRAVEL_Absolute);
	MarkStarted(TEXT("host travel to own session"));
}

void FEasySessionTravel::TravelToJoinedSession(const FString& ConnectString, const FString& Password, const FString& AdditionalTravelOptions)
{
	APlayerController* PlayerController = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		UE_LOG(LogEasySession, Warning, TEXT("No local player controller to travel with. Travel aborted."));
		return;
	}

	FString TravelURL = ConnectString;
	const FString TrimmedPassword = Password.TrimStartAndEnd();
	if (!TrimmedPassword.IsEmpty())
	{
		TravelURL += FString::Printf(TEXT("?%s=%s"), EasySession::TravelOption_Password,
			*EasySessionAddress::EncodeTravelOptionValue(TrimmedPassword));
	}
	AppendTravelOptions(TravelURL, AdditionalTravelOptions);
	Owner.OnModifyClientTravelURL.Broadcast(TravelURL);

	UE_LOG(LogEasySession, Log, TEXT("Traveling to host at '%s'"), *ConnectString);
	PlayerController->ClientTravel(TravelURL, TRAVEL_Absolute);
	MarkStarted(TEXT("client travel to joined session"));
}

void FEasySessionTravel::ReturnToMenu()
{
	UGameInstance* GameInstance = Owner.GetGameInstance();
	if (GameInstance == nullptr || GameInstance->GetWorld() == nullptr)
	{
		return;
	}

	// ReturnToMainMenu also clears what an OpenLevel would leave behind: the pending net game, the ?listen and ?LAN options, and the net driver.
	// The engine already owns the Game Default Map setting.
	// A second call does nothing once the engine is already browsing to the default map.
	UE_LOG(LogEasySession, Log, TEXT("Returning to the main menu (Game Default Map)."));
	GameInstance->ReturnToMainMenu();
	MarkStarted(TEXT("return to menu"));
}

void FEasySessionTravel::MarkStarted(const TCHAR* Reason)
{
	if (!bTravelInFlight)
	{
		UE_LOG(LogEasySession, Verbose, TEXT("Travel started (%s). Session operations report busy until the map is loaded."), Reason);
	}
	bTravelInFlight = true;
}

void FEasySessionTravel::CancelPendingTravel()
{
	UWorld* World = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	// The engine reads both URLs in TickWorldTravel on the next tick, so emptying them now cancels the travel.
	if (FWorldContext* Context = GEngine->GetWorldContextFromWorld(World))
	{
		Context->TravelURL.Empty();
	}
	World->NextURL.Empty();

	if (bTravelInFlight)
	{
		UE_LOG(LogEasySession, Log, TEXT("Canceled a travel that had not started loading its map yet."));
	}
	bTravelInFlight = false;
}

void FEasySessionTravel::HandlePostLoadMap(UWorld* LoadedWorld)
{
	// Fires for every world in the process, ours or another PIE instance's.
	if (LoadedWorld == nullptr || LoadedWorld->GetGameInstance() != Owner.GetGameInstance())
	{
		return;
	}

	bTravelInFlight = false;
}

void FEasySessionTravel::NotifyTravelFailed()
{
	bTravelInFlight = false;
}

void FEasySessionTravel::AppendTravelOptions(FString& InOutURL, const FString& Options)
{
	if (Options.IsEmpty())
	{
		return;
	}

	FString Normalized = Options;
	Normalized.RemoveFromStart(TEXT("?"));
	InOutURL += TEXT("?") + Normalized;
}
