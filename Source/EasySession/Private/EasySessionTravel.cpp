// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionTravel.h"

#include "EasySession.h"
#include "EasySessionAddress.h"
#include "EasySessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"
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

	const FString TravelURL = MakeServerTravelURL(HostParams.InitialMapName, HostParams.AdditionalTravelOptions, HostParams.MaxPlayers);

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

void FEasySessionTravel::TravelToJoinedSession(const FString& ConnectString, const FString& AdditionalTravelOptions)
{
	APlayerController* PlayerController = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		UE_LOG(LogEasySession, Warning, TEXT("No local player controller to travel with. Travel aborted."));
		return;
	}

	FString TravelURL = ConnectString;
	AppendTravelOptions(TravelURL, AdditionalTravelOptions);
	Owner.OnModifyClientTravelURL.Broadcast(TravelURL);

	UE_LOG(LogEasySession, Log, TEXT("Traveling to host at '%s'"), *ConnectString);
	PlayerController->ClientTravel(TravelURL, TRAVEL_Absolute);
	MarkStarted(TEXT("client travel to joined session"));
}

bool FEasySessionTravel::ServerTravelToMap(const FString& MapName)
{
	UWorld* World = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return false;
	}

	// The URL of the host's first travel carried MaxPlayers, but an Update request may have changed it since.
	int32 MaxPlayers = 0;
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(World);
	if (const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr)
	{
		MaxPlayers = NamedSession->SessionSettings.NumPublicConnections;
	}

	const FString TravelURL = MakeServerTravelURL(MapName, FString(), MaxPlayers);

	UE_LOG(LogEasySession, Log, TEXT("ServerTravel to '%s'"), *TravelURL);
	if (!World->ServerTravel(TravelURL))
	{
		return false;
	}

	MarkStarted(TEXT("server travel"));
	return true;
}

void FEasySessionTravel::ReturnToMenu()
{
	UGameInstance* GameInstance = Owner.GetGameInstance();
	if (GameInstance == nullptr || GameInstance->GetWorld() == nullptr)
	{
		return;
	}

	// ReturnToMainMenu also clears the pending net game, the ?listen and ?LAN options and the net driver, which OpenLevel keeps.
	// It travels to the engine's Game Default Map, so the plugin needs no menu map setting of its own.
	// A second call does nothing once the engine is already browsing to the default map.
	UE_LOG(LogEasySession, Log, TEXT("Returning to the main menu (Game Default Map)."));
	GameInstance->ReturnToMainMenu();
	MarkStarted(TEXT("return to menu"));
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

FString FEasySessionTravel::MakeServerTravelURL(const FString& MapName, const FString& AdditionalTravelOptions, int32 MaxPlayers) const
{
	FString TravelURL = MapName.TrimStartAndEnd();

	const UWorld* World = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	const bool bIsDedicatedServer = World != nullptr && World->GetNetMode() == NM_DedicatedServer;
	if (!bIsDedicatedServer && !EasySessionAddress::HasListenOption(TravelURL))
	{
		TravelURL += TEXT("?listen");
	}

	AppendTravelOptions(TravelURL, AdditionalTravelOptions);

	// The engine copies this option into AGameSession::MaxPlayers, so its "Server full" refusal matches the advertised NumPublicConnections.
	if (MaxPlayers > 0)
	{
		EasySessionAddress::AppendMaxPlayersOption(TravelURL, MaxPlayers);
	}

	Owner.OnModifyServerTravelURL.Broadcast(TravelURL);
	return TravelURL;
}

void FEasySessionTravel::MarkStarted(const TCHAR* Reason)
{
	if (!bTravelInFlight)
	{
		UE_LOG(LogEasySession, Verbose, TEXT("Travel started (%s). Session requests report busy until the map is loaded."), Reason);
	}
	bTravelInFlight = true;
}

void FEasySessionTravel::HandlePostLoadMap(UWorld* LoadedWorld)
{
	// The engine fires this delegate for every world in the process, including the worlds of other PIE instances.
	if (LoadedWorld == nullptr || LoadedWorld->GetGameInstance() != Owner.GetGameInstance())
	{
		return;
	}

	bTravelInFlight = false;
}
