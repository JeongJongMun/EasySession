// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionHost.h"

#include "EasySession.h"
#include "EasySessionJoinApproval.h"
#include "EasySessionJoinApprovalBeacon.h"
#include "EasySessionServerGate.h"
#include "EasySessionStateActor.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"

FEasySessionHost::FEasySessionHost(UEasySessionSubsystem& InOwner, FEasySessionBeaconPort& InBeaconPort)
	: Owner(InOwner)
	, Gate(MakeUnique<FEasySessionServerGate>(InOwner))
	, JoinApproval(MakeUnique<FEasySessionJoinApproval>(InOwner, InBeaconPort))
{
	Gate->Initialize();
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

	JoinApproval.Reset();
	Gate.Reset();
}

void FEasySessionHost::OnSessionCreated(const FEasySessionHostParams& Params)
{
	// This process created the session, so it is the session's server, on a dedicated server just as much as on a listen server.
	// bHosting is a member of the session object, so it cannot outlive the session.
	// NULL already sets it in CreateSession, and Steam never sets it.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr)
	{
		NamedSession->bHosting = true;
	}

	Gate->SetSessionCredentials(Params.Password.TrimStartAndEnd(), Params.bFriendsBypassPassword);
	EnsureStateActor();
}

void FEasySessionHost::OnSettingsUpdated(const FEasySessionSettings& Settings)
{
	Gate->SetSessionCredentials(Settings.Password.TrimStartAndEnd(), Settings.bFriendsBypassPassword);

	// Joined players learn about the update through the replicated state actor.
	UpdateStateActor();
}

void FEasySessionHost::OnMatchStateChanged()
{
	// The online subsystem only changes this game's own copy of the session, so the new state is replicated for the clients here now and the ones that join later.
	if (Owner.IsSessionAuthority())
	{
		UpdateStateActor();
	}
}

void FEasySessionHost::OnSessionDestroyed()
{
	Gate->ClearSessionCredentials();
	DestroyWorldActors();
}

void FEasySessionHost::SpawnWorldActors()
{
	EnsureStateActor();
	JoinApproval->EnsureHost();
}

void FEasySessionHost::DestroyWorldActors()
{
	JoinApproval->StopHost();

	if (AEasySessionStateActor* Actor = StateActor.Get())
	{
		Actor->Destroy();
	}
	StateActor.Reset();
}

FEasyJoinApprovalResponse FEasySessionHost::ApproveJoin(const FEasyJoinApprovalRequest& Request, const FUniqueNetIdRepl& Requester) const
{
	return Gate->ApproveJoin(Request, Requester);
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
