// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionTravel.h"

#include "EasySession.h"
#include "EasySessionAddress.h"
#include "EasySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

FEasySessionTravel::FEasySessionTravel(UEasySessionSubsystem& InOwner)
	: Owner(InOwner)
{
}

FEasySessionTravel::~FEasySessionTravel()
{
	if (ListenCheckTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ListenCheckTickerHandle);
		ListenCheckTickerHandle.Reset();
	}
}

void FEasySessionTravel::EnsureHostIsListening(const FEasySessionHostParams& HostParams)
{
	UWorld* World = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	const bool bIsDedicated = HostParams.HostMode == EEasySessionHostMode::DedicatedServer;

	if (!HostParams.MapName.IsEmpty())
	{
		TravelToOwnSession(HostParams);
	}
	else if (!bIsDedicated && HostParams.bStartListening && World->GetNetMode() == NM_Standalone)
	{
		// No map to travel to - start listening on the current map so clients can connect.
		FURL ListenURL;
		if (World->Listen(ListenURL))
		{
			UE_LOG(LogEasySession, Log, TEXT("Started listening on the current map (port %d)."), ListenURL.Port);
		}
		else
		{
			UE_LOG(LogEasySession, Warning, TEXT("Failed to start a listen server on the current map. Clients will not be able to connect."));
			Owner.OnSessionFailure.Broadcast(TEXT("Failed to start a listen server on the current map."));
		}
	}

	// Verify shortly after that we actually became a listen server - the most common
	// beginner pitfall is a session that is advertised but not connectable.
	if (!bIsDedicated && HostParams.bStartListening)
	{
		if (ListenCheckTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(ListenCheckTickerHandle);
		}

		ListenCheckTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime)
		{
			ListenCheckTickerHandle.Reset();

			const UWorld* CurrentWorld = Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
			if (CurrentWorld != nullptr && CurrentWorld->GetNetMode() == NM_Standalone && Owner.IsInSession() && Owner.IsHost())
			{
				UE_LOG(LogEasySession, Warning, TEXT("The session is advertised but this game is still not a listen server - clients will fail to connect. Common causes: PIE with 'Run Under One Process' enabled, an invalid travel map path, or the starting map's game mode using seamless travel (seamless travel drops the ?listen option)."));
			}
			return false;
		}), 3.0f);
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
	Owner.OnModifyServerTravelURL.Broadcast(TravelURL);

	UE_LOG(LogEasySession, Log, TEXT("Traveling to session map '%s'"), *TravelURL);

	// From a standalone game - the very first travel that turns the host into a
	// listen server - use an absolute client travel: it always performs a hard map
	// load, so ?listen is guaranteed to take effect. ServerTravel would consult the
	// current game mode's bUseSeamlessTravel, and seamless travel silently drops
	// ?listen - the travel must not depend on (or mutate) the game's configuration.
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
		MarkStarted(TEXT("host travel to own session"));
		return;
	}

	// Already a server (listen or dedicated): a regular server travel moves every
	// connected player along, and may be seamless.
	if (!World->ServerTravel(TravelURL))
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

void FEasySessionTravel::NotifyMapLoaded()
{
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
