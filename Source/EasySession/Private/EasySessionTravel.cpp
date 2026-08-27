// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionTravel.h"

#include "EasySession.h"
#include "EasySessionAddress.h"
#include "EasySessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
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

void FEasySessionTravel::ListenOnCurrentMap(const FEasySessionHostParams& HostParams)
{
	UWorld* World = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	const bool bIsDedicated = HostParams.HostMode == EEasySessionHostMode::DedicatedServer;
	if (bIsDedicated || !HostParams.bStartListening || World->GetNetMode() != NM_Standalone)
	{
		return;
	}

	FURL ListenURL;
	if (World->Listen(ListenURL))
	{
		UE_LOG(LogEasySession, Log, TEXT("Started listening on the current map (port %d)."), ListenURL.Port);

		// No travel URL here, so the engine never reads ?MaxPlayers= - the cap its "Server full" refusal compares against is set by hand.
		AGameModeBase* GameMode = World->GetAuthGameMode();
		if (GameMode && GameMode->GameSession)
		{
			GameMode->GameSession->MaxPlayers = HostParams.MaxPlayers;
		}
	}
	else
	{
		UE_LOG(LogEasySession, Warning, TEXT("Failed to start a listen server on the current map. Clients will not be able to connect."));
		Owner.OnSessionFailure.Broadcast(TEXT("Failed to start a listen server on the current map."));
	}
}

void FEasySessionTravel::TravelToOwnSession(const FEasySessionHostParams& HostParams)
{
	if (HostParams.MapName.IsEmpty())
	{
		return;
	}

	UWorld* World = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	FString TravelURL = HostParams.MapName;
	if (HostParams.HostMode == EEasySessionHostMode::ListenServer && HostParams.bStartListening && !EasySessionAddress::HasListenOption(TravelURL))
	{
		TravelURL += TEXT("?listen");
	}
	AppendTravelOptions(TravelURL, HostParams.AdditionalTravelOptions);
	// The engine reads this into AGameSession::MaxPlayers, so its "Server full" refusal matches the advertised capacity.
	EasySessionAddress::AppendMaxPlayersOption(TravelURL, HostParams.MaxPlayers);
	Owner.OnModifyServerTravelURL.Broadcast(TravelURL);

	UE_LOG(LogEasySession, Log, TEXT("Traveling to session map '%s'"), *TravelURL);

	// Not yet a server (hosting from the main menu). A client travel hard loads, so ?listen opens the listen server; seamless ServerTravel would drop it.
	if (World->GetNetMode() == NM_Standalone)
	{
		APlayerController* PlayerController = Owner.GetGameInstance()->GetFirstLocalPlayerController();
		if (PlayerController == nullptr)
		{
			UE_LOG(LogEasySession, Warning, TEXT("No local player controller to travel with. Travel to '%s' aborted."), *TravelURL);
			Owner.OnSessionFailure.Broadcast(FString::Printf(TEXT("Travel to '%s' failed."), *TravelURL));
			return;
		}

		PlayerController->ClientTravel(TravelURL, TRAVEL_Absolute);
	}
	// Already a server: no local player controller on a dedicated server, and a server travel brings connected players along.
	else if (!World->ServerTravel(TravelURL))
	{
		UE_LOG(LogEasySession, Warning, TEXT("ServerTravel to '%s' failed. Check that the map path is valid (e.g. /Game/Maps/Lobby)."), *TravelURL);
		Owner.OnSessionFailure.Broadcast(FString::Printf(TEXT("ServerTravel to '%s' failed."), *TravelURL));
		return;
	}

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

	// The engine reads both URLs in TickWorldTravel on the next tick, so emptying them now drops the travel.
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
