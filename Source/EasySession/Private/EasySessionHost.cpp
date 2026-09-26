// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionHost.h"

#include "EasySession.h"
#include "EasySessionBeaconPort.h"
#include "EasySessionReservations.h"
#include "EasySessionStateActor.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"

FEasySessionHost::FEasySessionHost(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort)
	: Owner(InOwner)
	, BeaconPort(InBeaconPort)
	, Reservations(MakeUnique<FEasySessionReservations>(InOwner, InBeaconPort))
{
	GameModeInitializedHandle = FGameModeEvents::GameModeInitializedEvent.AddRaw(this, &FEasySessionHost::HandleGameModeInitialized);
}

FEasySessionHost::~FEasySessionHost()
{
	if (GameModeInitializedHandle.IsValid())
	{
		FGameModeEvents::GameModeInitializedEvent.Remove(GameModeInitializedHandle);
		GameModeInitializedHandle.Reset();
	}

	FTSTicker::GetCoreTicker().RemoveTicker(DeferredSetUpHandle);
	DeferredSetUpHandle.Reset();

	Reservations.Reset();
}

void FEasySessionHost::OnSessionCreated(const FEasySessionHostParams& Params)
{
	// This process created the session, so it is the host. NULL already sets bHosting in CreateSession, but Steam never does.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr)
	{
		NamedSession->bHosting = true;
	}

	Reservations->OnSessionCreated(Params);
}

void FEasySessionHost::OnSettingsUpdated(const FEasySessionSettings& Settings)
{
	// The engine's player cap follows the advertised one, so its "Server full" refusal uses the new Max Players.
	UWorld* World = GetWorld();
	AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
	if (GameMode && GameMode->GameSession)
	{
		GameMode->GameSession->MaxPlayers = Settings.MaxPlayers;
	}

	// UpdateSession never recomputes the open slot count, so it is recomputed from the registered players.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(World);
	if (FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr)
	{
		NamedSession->NumOpenPublicConnections =
			FMath::Max(0, NamedSession->SessionSettings.NumPublicConnections - NamedSession->RegisteredPlayers.Num());
	}

	Reservations->OnSettingsUpdated(Settings);

	// Joined players learn about the update through the replicated state actor.
	UpdateStateActor();

	// A client broadcasts this when the replicated settings arrive, so the host broadcasts it for its own UI here.
	Owner.OnSessionSettingsChanged.Broadcast();
}

void FEasySessionHost::OnMatchStateChanged()
{
	// The online subsystem only changes this game's own copy of the session.
	// The new state is replicated here, for the clients in the session now and the ones that join later.
	UpdateStateActor();
}

void FEasySessionHost::OnSessionDestroyed()
{
	Reservations->OnSessionDestroyed();
	DestroyStateActor();
}

void FEasySessionHost::OnServerTravelStarted()
{
	Reservations->OnServerTravelStarted();
	DestroyStateActor();

	// The next world starts its own beacon listener, which can only bind the beacon port after this one released it.
	BeaconPort.ReleaseListener();
}

void FEasySessionHost::OnServerTravelFailed()
{
	// The travel never left this world, and OnServerTravelStarted already took its actors down.
	SpawnWorldActors();
}

void FEasySessionHost::SpawnWorldActors()
{
	EnsureStateActor();
	Reservations->StartBeacon();
}

void FEasySessionHost::DestroyStateActor()
{
	if (AEasySessionStateActor* Actor = StateActor.Get())
	{
		Actor->Destroy();
	}
	StateActor.Reset();
}

void FEasySessionHost::TellEveryoneToReturnToMenu(const FText& Reason)
{
	if (AEasySessionStateActor* Actor = StateActor.Get())
	{
		Actor->MulticastReturnToMenu(Reason);
	}
}

void FEasySessionHost::HandleGameModeInitialized(AGameModeBase* GameMode)
{
	// Fires on the server for every world, ours or another PIE instance's.
	const UWorld* InitializedWorld = GameMode != nullptr ? GameMode->GetWorld() : nullptr;
	if (InitializedWorld == nullptr || InitializedWorld->GetGameInstance() != Owner.GetGameInstance())
	{
		return;
	}

	if (!Owner.IsSessionAuthority())
	{
		return;
	}

	// One tick later, so a beacon listener the project spawns in RegisterServer or BeginPlay exists first and gets reused.
	FTSTicker::GetCoreTicker().RemoveTicker(DeferredSetUpHandle);
	DeferredSetUpHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
	{
		DeferredSetUpHandle.Reset();

		// Every travel creates a new world, so the state actor of the previous world is destroyed.
		StateActor.Reset();
		SpawnWorldActors();
		return false;
	}));
}

void FEasySessionHost::EnsureStateActor()
{
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_Client)
	{
		return;
	}

	if (!StateActor.IsValid() || StateActor->GetWorld() != World)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		StateActor = World->SpawnActor<AEasySessionStateActor>(SpawnParams);
	}

	UpdateStateActor();
}

void FEasySessionHost::UpdateStateActor()
{
	AEasySessionStateActor* Actor = StateActor.Get();
	if (Actor == nullptr)
	{
		return;
	}

	// This runs on the host, where GetSessionState returns the local state rather than a replicated one.
	Actor->SetHostSessionState(Owner.GetSessionState());

	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr)
	{
		Actor->SetReplicatedSessionSettings(MakeReplicatedSettings(NamedSession->SessionSettings));
	}
}

UWorld* FEasySessionHost::GetWorld() const
{
	return Owner.GetGameInstance() ? Owner.GetGameInstance()->GetWorld() : nullptr;
}

FEasySessionReplicatedSettings FEasySessionHost::MakeReplicatedSettings(const FOnlineSessionSettings& Settings)
{
	FEasySessionReplicatedSettings Payload;
	Payload.MaxPlayers = Settings.NumPublicConnections;
	Payload.bShouldAdvertise = Settings.bShouldAdvertise;
	Payload.bAllowJoinInProgress = Settings.bAllowJoinInProgress;
	Payload.bAllowInvites = Settings.bAllowInvites;
	Payload.bValid = true;

	for (const TPair<FName, FOnlineSessionSetting>& Setting : Settings.Settings)
	{
		if (Setting.Key == EasySession::SettingKey_DisplayName)
		{
			Payload.SessionDisplayName = Setting.Value.Data.ToString();
		}
		else if (Setting.Key == EasySession::SettingKey_Hidden)
		{
			int32 Hidden = 0;
			Setting.Value.Data.GetValue(Hidden);
			Payload.bHidden = Hidden != 0;
		}
		else if (Setting.Key == EasySession::SettingKey_PasswordProtected)
		{
			int32 Protected = 0;
			Setting.Value.Data.GetValue(Protected);
			Payload.bPasswordProtected = Protected != 0;
		}
		else if (Setting.Key == EasySession::SettingKey_Region)
		{
			int32 RegionValue = 0;
			Setting.Value.Data.GetValue(RegionValue);
			Payload.Region = static_cast<EEasySessionRegion>(RegionValue);
		}
		else if (Setting.Key == EasySession::SettingKey_JoinCode)
		{
			Setting.Value.Data.GetValue(Payload.JoinCode);
		}
		else if (!EasySession::IsReservedSettingKey(Setting.Key))
		{
			FEasySessionReplicatedSetting Custom;
			Custom.Key = Setting.Key.ToString();
			Custom.Value = Setting.Value.Data.ToString();
			Payload.CustomSettings.Add(MoveTemp(Custom));
		}
	}

	return Payload;
}
