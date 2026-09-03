// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionSubsystem.h"

#include "EasyMatchmakingPolicy.h"
#include "EasySession.h"
#include "EasySessionAddress.h"
#include "EasySessionOperation.h"
#include "EasySessionRequest.h"
#include "EasySessionRequestQueue.h"
#include "EasySessionServerGate.h"
#include "EasySessionSocial.h"
#include "EasySessionStateActor.h"
#include "EasySessionTravel.h"
#include "EasySessionDiagnostics.h"
#include "EasySessionJoinApproval.h"
#include "EasySessionConfig.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/UObjectGlobals.h"

// Defined here, where the collaborator types are complete.
UEasySessionSubsystem::UEasySessionSubsystem() = default;
UEasySessionSubsystem::UEasySessionSubsystem(FVTableHelper& Helper) : Super(Helper) {}
UEasySessionSubsystem::~UEasySessionSubsystem() = default;

void UEasySessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub == nullptr)
	{
		UE_LOG(LogEasySession, Warning, TEXT("No online subsystem found. Check [OnlineSubsystem] DefaultPlatformService in DefaultEngine.ini."));
	}
	else
	{
		UE_LOG(LogEasySession, Log, TEXT("EasySessionSubsystem initialized. Online subsystem: %s"), *OnlineSub->GetSubsystemName().ToString());
	}

	RequestQueue = MakeUnique<FEasySessionRequestQueue>(
		[this]() { ExecuteActiveRequest(); },
		[this]() { HandleRequestDeadline(); });
	Travel = MakeUnique<FEasySessionTravel>(*this);
	Social = MakeUnique<FEasySessionSocial>(*this);
	ServerGate = MakeUnique<FEasySessionServerGate>(*this);
	ServerGate->Initialize();
	JoinApproval = MakeUnique<FEasySessionJoinApproval>(*this);
	JoinApproval->Initialize();

	if (GEngine != nullptr)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UEasySessionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UEasySessionSubsystem::HandleTravelFailure);
	}

	// The session interface may not be reachable until the world exists - retry until it is.
	InviteBindTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float /*DeltaTime*/)
	{
		if (!GetSessionInterface().IsValid())
		{
			return true;
		}

		Social->BindInviteDelegates();
#if !UE_BUILD_SHIPPING
		// The fixes it prints are for whoever builds the game, not whoever plays it.
		// Packaged development builds keep it: that is where a service that works in
		// the editor and not in a build gets diagnosed.
		EasySessionDiagnostics::LogReport(EasySessionDiagnostics::RunDiagnostics(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr));
#endif
		InviteBindTickerHandle.Reset();
		return false;
	}), 0.5f);

	WorldInitializedActorsHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &UEasySessionSubsystem::HandleWorldInitializedActors);

	// Requests finish on a later tick and travels end inside the engine, so the busy flag is watched here rather than at every call site.
	BusyTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float /*DeltaTime*/)
	{
		RefreshBusyState();
		return true;
	}));

	if (IsRunningDedicatedServer() && GetDefault<UEasySessionConfig>()->bAutoHostOnDedicatedServer)
	{
		DedicatedAutoHostTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float DeltaTime)
		{
			const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			if (World == nullptr || !World->HasBegunPlay())
			{
				return true;
			}

			AutoHostDedicatedServerSession();
			DedicatedAutoHostTickerHandle.Reset();
			return false;
		}), 0.5f);
	}
}

void UEasySessionSubsystem::Deinitialize()
{
	if (GEngine != nullptr && NetworkFailureHandle.IsValid())
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		NetworkFailureHandle.Reset();
	}

	if (GEngine != nullptr && TravelFailureHandle.IsValid())
	{
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
		TravelFailureHandle.Reset();
	}

	if (WorldInitializedActorsHandle.IsValid())
	{
		FWorldDelegates::OnWorldInitializedActors.Remove(WorldInitializedActorsHandle);
		WorldInitializedActorsHandle.Reset();
	}

	if (DedicatedAutoHostTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DedicatedAutoHostTickerHandle);
		DedicatedAutoHostTickerHandle.Reset();
	}

	if (InviteBindTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(InviteBindTickerHandle);
		InviteBindTickerHandle.Reset();
	}

	if (BusyTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(BusyTickerHandle);
		BusyTickerHandle.Reset();
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
		Sessions->ClearOnFindFriendSessionCompleteDelegate_Handle(0, FindFriendCompleteHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle);
		Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateCompleteHandle);
		Sessions->ClearOnStartSessionCompleteDelegate_Handle(StartCompleteHandle);
		Sessions->ClearOnEndSessionCompleteDelegate_Handle(EndCompleteHandle);
	}

	// Operations end themselves when canceled, and they may still hold a step's delegate - cancel before the queue goes.
	RequestQueue->CancelOperations();

	// Destroying these unbinds everything they registered, tickers included.
	RequestQueue.Reset();
	Travel.Reset();
	Social.Reset();
	ServerGate.Reset();
	JoinApproval.Reset();

	ActiveMatchmakingPolicy = nullptr;
	ActiveSearch.Reset();

	Super::Deinitialize();
}

void UEasySessionSubsystem::CreateEasySession(const FEasySessionHostParams& HostParams, FEasySessionCompleteDelegate OnComplete)
{
	TSharedRef<FEasySessionRequest> Request = MakeShared<FEasySessionRequest>(FEasySessionRequest::EType::Create);
	Request->HostParams = HostParams;
	Request->OnComplete = MoveTemp(OnComplete);
	EnqueueRequest(Request);
}

void UEasySessionSubsystem::FindEasySessions(const FEasySessionSearchParams& SearchParams, FEasySessionFindCompleteDelegate OnComplete)
{
	TSharedRef<FEasySessionRequest> Request = MakeShared<FEasySessionRequest>(FEasySessionRequest::EType::Find);
	Request->SearchParams = SearchParams;
	Request->OnFindComplete = MoveTemp(OnComplete);

	// A targeted query names its room, hidden or not - and the hidden-seeing mark keeps it off the public search surfaces.
	if (Request->SearchParams.IsSpecificSessionQuery())
	{
		Request->SearchParams.bIncludeHiddenSessions = true;
	}

	EnqueueRequest(Request);
}

void UEasySessionSubsystem::JoinEasySession(const FEasySessionSearchResult& SearchResult, const FString& Password, const FString& AdditionalTravelOptions, FEasySessionCompleteDelegate OnComplete)
{
	TSharedRef<FEasySessionRequest> Request = MakeShared<FEasySessionRequest>(FEasySessionRequest::EType::Join);
	Request->JoinTarget = SearchResult;
	Request->JoinPassword = Password;
	Request->JoinTravelOptions = AdditionalTravelOptions;
	Request->OnComplete = MoveTemp(OnComplete);
	EnqueueRequest(Request);
}

void UEasySessionSubsystem::StartEasySession(FEasySessionCompleteDelegate OnComplete)
{
	TSharedRef<FEasySessionRequest> Request = MakeShared<FEasySessionRequest>(FEasySessionRequest::EType::Start);
	Request->OnComplete = MoveTemp(OnComplete);
	EnqueueRequest(Request);
}

void UEasySessionSubsystem::EndEasySession(FEasySessionCompleteDelegate OnComplete)
{
	TSharedRef<FEasySessionRequest> Request = MakeShared<FEasySessionRequest>(FEasySessionRequest::EType::End);
	Request->OnComplete = MoveTemp(OnComplete);
	EnqueueRequest(Request);
}

void UEasySessionSubsystem::DestroyEasySession(FEasySessionCompleteDelegate OnComplete)
{
	TSharedRef<FEasySessionRequest> Request = MakeShared<FEasySessionRequest>(FEasySessionRequest::EType::Destroy);
	Request->OnComplete = MoveTemp(OnComplete);
	EnqueueRequest(Request);
}

void UEasySessionSubsystem::LeaveEasySession(FEasySessionCompleteDelegate OnComplete)
{
	// A leaving host takes the room with it - closing it for everyone tells each client why before the connection dies.
	if (IsSessionAuthority())
	{
		DestroyEasySessionForEveryone(NSLOCTEXT("EasySession", "HostLeftSession", "The host has left the game."), MoveTemp(OnComplete));
		return;
	}
	
	DestroyEasySession(FEasySessionCompleteDelegate::CreateWeakLambda(this,
		[this, OnComplete](EEasySessionResult Result, const FString& ErrorMessage)
		{
			// Requested before the completion below, the same order every travel in this plugin uses.
			ReturnToMenu();
			OnComplete.ExecuteIfBound(Result, ErrorMessage);
		}));
}

void UEasySessionSubsystem::UpdateEasySession(const FEasySessionSettings& NewSettings, FEasySessionCompleteDelegate OnComplete)
{
	TSharedRef<FEasySessionRequest> Request = MakeShared<FEasySessionRequest>(FEasySessionRequest::EType::Update);
	Request->Settings = NewSettings;
	Request->OnComplete = MoveTemp(OnComplete);
	EnqueueRequest(Request);
}

void UEasySessionSubsystem::StartMatchmaking(const FEasyMatchmakingParams& MatchmakingParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass, FEasySessionCompleteDelegate OnComplete)
{
	if (IsMatchmakingRunning())
	{
		OnComplete.ExecuteIfBound(EEasySessionResult::MatchmakingAlreadyInProgress, TEXT("Matchmaking is already running. Cancel it first."));
		return;
	}

	UEasyMatchmakingPolicy* Policy = NewObject<UEasyMatchmakingPolicy>(this, PolicyClass != nullptr ? PolicyClass.Get() : UEasyMatchmakingPolicy::StaticClass());
	ActiveMatchmakingPolicy = Policy;
	Policy->OnStateChanged.AddDynamic(this, &UEasySessionSubsystem::RelayMatchmakingStateChanged);
	Policy->OnUpdated.AddDynamic(this, &UEasySessionSubsystem::RelayMatchmakingUpdated);
	OnMatchmakingStarted.Broadcast();

	Policy->Start(*this, MatchmakingParams, FEasySessionCompleteDelegate::CreateWeakLambda(this,
		[this, UserDelegate = MoveTemp(OnComplete)](EEasySessionResult Result, const FString& ErrorMessage)
		{
			ActiveMatchmakingPolicy = nullptr;
			// Observers first: the requester's delegate often tears down the very UI that is listening.
			OnMatchmakingComplete.Broadcast(Result, ErrorMessage);
			UserDelegate.ExecuteIfBound(Result, ErrorMessage);
		}));
}

void UEasySessionSubsystem::RelayMatchmakingStateChanged(EEasyMatchmakingState OldState, EEasyMatchmakingState NewState)
{
	OnMatchmakingStateChanged.Broadcast(OldState, NewState);
}

void UEasySessionSubsystem::RelayMatchmakingUpdated(EEasyMatchmakingState MatchmakingState, int32 ElapsedSeconds)
{
	OnMatchmakingUpdated.Broadcast(MatchmakingState, ElapsedSeconds);
}

void UEasySessionSubsystem::CancelMatchmaking()
{
	if (ActiveMatchmakingPolicy != nullptr)
	{
		ActiveMatchmakingPolicy->Cancel();
	}
}

bool UEasySessionSubsystem::IsMatchmakingRunning() const
{
	return ActiveMatchmakingPolicy != nullptr && ActiveMatchmakingPolicy->GetState() != EEasyMatchmakingState::Idle && ActiveMatchmakingPolicy->GetState() != EEasyMatchmakingState::Complete;
}

EEasyMatchmakingState UEasySessionSubsystem::GetMatchmakingState() const
{
	return ActiveMatchmakingPolicy != nullptr ? ActiveMatchmakingPolicy->GetState() : EEasyMatchmakingState::Idle;
}

bool UEasySessionSubsystem::IsInSession() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	return Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr;
}

EEasySessionState UEasySessionSubsystem::GetSessionState() const
{
	const EEasySessionState LocalState = GetLocalSessionState();

	// Clients report the host's replicated state: the session lifecycle lives on the
	// host, and every player should agree on it regardless of when they joined.
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (LocalState != EEasySessionState::NoSession && bHasReplicatedHostSessionState &&
		World != nullptr && World->GetNetMode() == NM_Client)
	{
		return ReplicatedHostSessionState;
	}

	return LocalState;
}

FEasySessionSettings UEasySessionSubsystem::GetSessionSettings() const
{
	FEasySessionSettings Params;

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession == nullptr)
	{
		return Params;
	}

	const FOnlineSessionSettings& Settings = NamedSession->SessionSettings;
	Params.MaxPlayers = Settings.NumPublicConnections;
	Params.bShouldAdvertise = Settings.bShouldAdvertise;
	Params.bAllowJoinInProgress = Settings.bAllowJoinInProgress;
	Params.bAllowInvites = Settings.bAllowInvites;

	for (const TPair<FName, FOnlineSessionSetting>& Setting : Settings.Settings)
	{
		if (Setting.Key == EasySession::SettingKey_DisplayName)
		{
			Params.SessionDisplayName = Setting.Value.Data.ToString();
		}
		else if (Setting.Key == EasySession::SettingKey_Hidden)
		{
			int32 Hidden = 0;
			Setting.Value.Data.GetValue(Hidden);
			Params.bHidden = Hidden != 0;
		}
		else if (Setting.Key == EasySession::SettingKey_Region)
		{
			int32 RegionValue = 0;
			Setting.Value.Data.GetValue(RegionValue);
			Params.Region = static_cast<EEasySessionRegion>(RegionValue);
		}
		else if (Setting.Key == EasySession::SettingKey_JoinCode)
		{
			FString JoinCode;
			Setting.Value.Data.GetValue(JoinCode);
			Params.bUseJoinCode = !JoinCode.IsEmpty();
		}
		else if (!EasySession::IsReservedSettingKey(Setting.Key))
		{
			Params.CustomSettings.Add(Setting.Key.ToString(), Setting.Value.Data.ToString());
		}
	}

	// In the clear on purpose: this game already holds the password to check players
	// against, and blanking it here would leave no way to remove one through Update.
	if (ServerGate.IsValid())
	{
		Params.Password = ServerGate->GetSessionPassword();
		Params.bFriendsBypassPassword = ServerGate->GetFriendsBypassPassword();
	}

	return Params;
}

FString UEasySessionSubsystem::GetSessionJoinCode() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession == nullptr)
	{
		return FString();
	}

	FString JoinCode;
	NamedSession->SessionSettings.Get(EasySession::SettingKey_JoinCode, JoinCode);
	return JoinCode;
}

EEasySessionState UEasySessionSubsystem::GetLocalSessionState() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession == nullptr)
	{
		return EEasySessionState::NoSession;
	}

	switch (NamedSession->SessionState)
	{
		case EOnlineSessionState::Creating:		return EEasySessionState::Creating;
		case EOnlineSessionState::Pending:		return EEasySessionState::Pending;
		case EOnlineSessionState::Starting:		return EEasySessionState::Starting;
		case EOnlineSessionState::InProgress:	return EEasySessionState::InProgress;
		case EOnlineSessionState::Ending:		return EEasySessionState::Ending;
		case EOnlineSessionState::Ended:		return EEasySessionState::Ended;
		case EOnlineSessionState::Destroying:	return EEasySessionState::Destroying;
		default:								return EEasySessionState::NoSession;
	}
}

bool UEasySessionSubsystem::IsNetworkServer() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return World != nullptr && World->GetNetMode() != NM_Client;
}

bool UEasySessionSubsystem::IsSessionAuthority() const
{
	return bCreatedActiveSession && IsInSession();
}

bool UEasySessionSubsystem::IsHost() const
{
	// A dedicated server has no local player, so it can never be the hosting player.
	// NULL sets bHosting when it creates a session (OnlineSessionInterfaceNull.cpp), which would otherwise report a LAN dedicated server as the host.
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World != nullptr && World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return false;
	}

	const FNamedOnlineSession* NamedSession = Sessions->GetNamedSession(NAME_GameSession);
	if (NamedSession == nullptr)
	{
		return false;
	}

	// NULL sets bHosting when creating a session, but Steam never touches the flag -
	// fall back to comparing the session owner's id with the local player (ids, not
	// names, for the same reason as the host marker in GetSessionPlayerInfos).
	if (NamedSession->bHosting)
	{
		return true;
	}

	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	const FUniqueNetIdRepl LocalId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();
	return NamedSession->OwningUserId.IsValid() && LocalId.GetUniqueNetId().IsValid() && *LocalId.GetUniqueNetId() == *NamedSession->OwningUserId;
}

TArray<FString> UEasySessionSubsystem::GetSessionPlayerNames() const
{
	TArray<FString> PlayerNames;

	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (GameState == nullptr)
	{
		return PlayerNames;
	}

	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (PlayerState != nullptr)
		{
			PlayerNames.Add(PlayerState->GetPlayerName());
		}
	}

	return PlayerNames;
}

FString UEasySessionSubsystem::GetSessionPassword() const
{
	return ServerGate.IsValid() ? ServerGate->GetSessionPassword() : FString();
}

FString UEasySessionSubsystem::GetSessionDisplayName() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession == nullptr)
	{
		return FString();
	}

	FString DisplayName;
	NamedSession->SessionSettings.Get(EasySession::SettingKey_DisplayName, DisplayName);
	return DisplayName;
}

TArray<FEasySessionPlayerInfo> UEasySessionSubsystem::GetSessionPlayerInfos() const
{
	TArray<FEasySessionPlayerInfo> Infos;

	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (GameState == nullptr)
	{
		return Infos;
	}

	const APlayerController* LocalController = GetGameInstance()->GetFirstLocalPlayerController();
	const APlayerState* LocalPlayerState = LocalController ? LocalController->PlayerState : nullptr;

	// The session owner's id identifies the host player. Ids are compared instead of
	// names because the engine truncates player names on login (InitNewPlayer).
	// Unset on dedicated servers, where no player row gets the host marker.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	const FUniqueNetIdPtr HostId = NamedSession ? NamedSession->OwningUserId : nullptr;

	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (PlayerState == nullptr)
		{
			continue;
		}

		const FUniqueNetIdRepl& PlayerId = PlayerState->GetUniqueId();

		FEasySessionPlayerInfo& Info = Infos.AddDefaulted_GetRef();
		Info.PlayerName = PlayerState->GetPlayerName();
		Info.bIsLocalPlayer = PlayerState == LocalPlayerState;
		Info.bIsHost = HostId.IsValid() && PlayerId.GetUniqueNetId().IsValid() && *PlayerId.GetUniqueNetId() == *HostId;
		Info.PlayerId = PlayerId;
	}

	return Infos;
}

int32 UEasySessionSubsystem::GetSessionPlayerCount() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->PlayerArray.Num() : 0;
}

int32 UEasySessionSubsystem::GetSessionMaxPlayers() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	return NamedSession ? NamedSession->SessionSettings.NumPublicConnections : 0;
}

bool UEasySessionSubsystem::IsBusy() const
{
	// Matchmaking counts because the queue empties between its steps.
	// Travel counts because the level load is the end of the operation.
	return RequestQueue->IsBusy() || IsMatchmakingRunning() || Travel->IsTraveling();
}

namespace
{
	/** The activity a busy operation shows as. Only operations that count as busy reach here. */
	EEasySessionActivity OperationActivity(EEasySessionOperationType Type)
	{
		switch (Type)
		{
			case EEasySessionOperationType::Matchmaking: return EEasySessionActivity::Matchmaking;
			case EEasySessionOperationType::FriendSearch:
			default:
				return EEasySessionActivity::None;
		}
	}
}

EEasySessionActivity UEasySessionSubsystem::GetActivity() const
{
	// Matchmaking wins over its own steps. Each step is a queued request, and naming
	// the steps would flicker between Searching and Joining during one run.
	if (IsMatchmakingRunning())
	{
		return EEasySessionActivity::Matchmaking;
	}

	if (const TSharedPtr<IEasySessionOperation> Operation = RequestQueue->FindBusyOperation())
	{
		return OperationActivity(Operation->GetType());
	}

	const TOptional<FEasySessionRequest::EType> Type = RequestQueue->GetCurrentType();
	if (Type.IsSet())
	{
		switch (Type.GetValue())
		{
			case FEasySessionRequest::EType::Create: return EEasySessionActivity::Creating;
			case FEasySessionRequest::EType::Find: return EEasySessionActivity::Searching;
			case FEasySessionRequest::EType::Join: return EEasySessionActivity::Joining;
			case FEasySessionRequest::EType::Destroy: return EEasySessionActivity::Leaving;
			case FEasySessionRequest::EType::Update: return EEasySessionActivity::Updating;
			case FEasySessionRequest::EType::Start: return EEasySessionActivity::Starting;
			case FEasySessionRequest::EType::End: return EEasySessionActivity::Ending;
		}
	}

	return Travel->IsTraveling() ? EEasySessionActivity::Traveling : EEasySessionActivity::None;
}

FString UEasySessionSubsystem::GetQueueStatus() const
{
	return RequestQueue->DescribeStatus(Travel->IsTraveling());
}

void UEasySessionSubsystem::RefreshBusyState()
{
	const bool bBusy = IsBusy();
	if (bBusy == bLastReportedBusy)
	{
		return;
	}

	bLastReportedBusy = bBusy;
	OnBusyChanged.Broadcast(bBusy);
}

FName UEasySessionSubsystem::GetOnlineSubsystemName() const
{
	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	return OnlineSub ? OnlineSub->GetSubsystemName() : NAME_None;
}

bool UEasySessionSubsystem::IsOnlineSubsystemAvailable() const
{
	return GetSessionInterface().IsValid();
}

bool UEasySessionSubsystem::ServerTravelToMap(const FString& MapName)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr || MapName.IsEmpty())
	{
		return false;
	}

	// UWorld::ServerTravel does not refuse a client - with no game mode it still sets
	// NextURL and returns true - so this entry check is the only gate.
	if (!IsSessionAuthority())
	{
		UE_LOG(LogEasySession, Warning, TEXT("ServerTravelToMap can only be called by the game hosting the session."));
		return false;
	}

	FString TravelURL = MapName;
	if (World->GetNetMode() != NM_DedicatedServer && !EasySessionAddress::HasListenOption(TravelURL))
	{
		TravelURL += TEXT("?listen");
	}

	// Every travel carries the current capacity: a host that listened on its first map has no
	// earlier URL for the engine to inherit it from, and an update may have changed it since.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession != nullptr)
	{
		EasySessionAddress::AppendMaxPlayersOption(TravelURL, NamedSession->SessionSettings.NumPublicConnections);
	}

	UE_LOG(LogEasySession, Log, TEXT("ServerTravel to '%s'"), *TravelURL);
	if (!World->ServerTravel(TravelURL))
	{
		return false;
	}

	// The map changes on the next frame, and the arrival world starts its own beacon.
	// Stopping now frees the beacon port in between - the new beacon fails to bind without this.
	JoinApproval->StopHost();

	Travel->MarkStarted(TEXT("ServerTravelToMap"));
	return true;
}

void UEasySessionSubsystem::CancelPendingTravel()
{
	Travel->CancelPendingTravel();
}

bool UEasySessionSubsystem::IsSessionBeingDestroyed() const
{
	return RequestQueue.IsValid() && RequestQueue->Contains(FEasySessionRequest::EType::Destroy);
}

IOnlineSessionPtr UEasySessionSubsystem::GetSessionInterface() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return Online::GetSessionInterface(World);
}

bool UEasySessionSubsystem::ShouldForceLAN() const
{
	return GetOnlineSubsystemName() == NULL_SUBSYSTEM;
}

void UEasySessionSubsystem::EnqueueRequest(TSharedRef<FEasySessionRequest> Request)
{
	// Where a request's target session is decided. The execute and complete handlers read it from the request; queries and gates are game session only and read the constant.
	Request->SessionName = NAME_GameSession;

	RequestQueue->Enqueue(Request);

	// Reported right away, so the UI disables its buttons on the same frame as the click.
	RefreshBusyState();
}

const TSharedPtr<FEasySessionRequest>& UEasySessionSubsystem::GetActiveRequest() const
{
	return RequestQueue->GetActive();
}

void UEasySessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// Every net driver reports here, so a beacon query timing out or a replay hiccup
	// would otherwise tear the session down. Only the game connection counts: the
	// world's driver, and the pending one a client uses while still traveling.
	if (NetDriver != nullptr &&
		NetDriver->NetDriverName != NAME_GameNetDriver &&
		NetDriver->NetDriverName != NAME_PendingNetDriver)
	{
		return;
	}

	const UWorld* OwnWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World != nullptr)
	{
		if (World != OwnWorld)
		{
			return;
		}
	}
	else
	{
		// A pending-connection failure broadcasts with no world to match against.
		// Being in a session is what says this instance was the one joining.
		if (!IsInSession())
		{
			return;
		}
	}

	const FString Reason = FString::Printf(TEXT("%s: %s"), ENetworkFailure::ToString(FailureType), *ErrorString);
	UE_LOG(LogEasySession, Warning, TEXT("Network failure: %s"), *Reason);
	OnSessionFailure.Broadcast(Reason);

	if (IsSessionAuthority())
	{
		// On the host this fires for a client whose connection died, not the host's
		// own. The session is still alive, so returning keeps the host from sending
		// itself back to the menu over someone else's disconnect.
		return;
	}

	// Only these two carry a sentence written for the player - a wrong password, a full
	// match, a version mismatch. The first arrives while joining, the second once
	// connected. Every other type leaves a debug dump, which belongs in the log.
	const bool bHostRefused =
		(FailureType == ENetworkFailure::PendingConnectionFailure ||
			FailureType == ENetworkFailure::FailureReceived) &&
		!ErrorString.IsEmpty();

	const FText PopupText = bHostRefused
		? FText::FromString(ErrorString)
		: NSLOCTEXT("EasySession", "LostConnectionToHost", "Lost connection to the host.");

	NotifyDisconnectedFromSession(
		bHostRefused ? EEasyDisconnectReason::Rejected : EEasyDisconnectReason::ConnectionLost,
		PopupText);
}

void UEasySessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	const UWorld* OwnWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World != OwnWorld)
	{
		return;
	}

	// The travel is over even though no map was loaded. The recovery below may start
	// a new one, which marks itself.
	Travel->NotifyTravelFailed();

	const FString Reason = FString::Printf(TEXT("%s: %s"), ETravelFailure::ToString(FailureType), *ErrorString);
	UE_LOG(LogEasySession, Warning, TEXT("Travel failure: %s"), *Reason);
	OnSessionFailure.Broadcast(Reason);

	// A failed server travel leaves the host's world, session and players untouched - only the map change failed, which OnSessionFailure just reported.
	if (IsSessionAuthority())
	{
		// ServerTravelToMap stopped the beacon for a world that never arrived.
		JoinApproval->EnsureHost();
		return;
	}

	NotifyDisconnectedFromSession(EEasyDisconnectReason::TravelFailure, FText::FromString(Reason));
}

void UEasySessionSubsystem::NotifyDisconnectedFromSession(EEasyDisconnectReason Reason, const FText& ReasonText)
{
	// First reason wins: tearing the session down can fail on its own (the connection
	// dropping while we leave), and those follow-up failures would replace the real
	// cause with a symptom. Only the reason is protected, though - reading it is
	// optional, so a reason nobody collected must never stop a later disconnect from
	// being cleaned up.
	if (!bHasPendingDisconnectInfo)
	{
		LastDisconnectInfo.Reason = Reason;
		LastDisconnectInfo.ReasonText = ReasonText;
		bHasPendingDisconnectInfo = true;
	}

	const bool bReturnToMenu = GetDefault<UEasySessionConfig>()->bAutoReturnToMenuOnDisconnect;

	if (IsInSession())
	{
		// A destroy already on the queue empties the session before a second one would run, so a second only adds a NoSessionExists failure.
		if (RequestQueue->Contains(FEasySessionRequest::EType::Destroy))
		{
			return;
		}

		// Clean up the dead session so the player can host or join again right away.
		DestroyEasySession(FEasySessionCompleteDelegate::CreateWeakLambda(this,
			[this, bReturnToMenu](EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/)
			{
				if (bReturnToMenu)
				{
					ReturnToMenu();
				}
			}));
	}
	else if (bReturnToMenu)
	{
		ReturnToMenu();
	}
}

FEasyDisconnectInfo UEasySessionSubsystem::ConsumeLastDisconnectInfo()
{
	const FEasyDisconnectInfo Info = LastDisconnectInfo;
	LastDisconnectInfo = FEasyDisconnectInfo();
	bHasPendingDisconnectInfo = false;
	return Info;
}

void UEasySessionSubsystem::ReturnToMenu()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance == nullptr || GameInstance->GetWorld() == nullptr)
	{
		return;
	}

	// ReturnToMainMenu does the cleanup an OpenLevel would skip - the pending net game, the ?listen/?LAN options, the net driver - and the engine already owns the "where is the menu" setting.
	// Repeated calls are harmless: the engine no-ops once it is already browsing to the default map.
	UE_LOG(LogEasySession, Log, TEXT("Returning to the main menu (Game Default Map)."));
	GameInstance->ReturnToMainMenu();
	Travel->MarkStarted(TEXT("return to menu"));
}

// Invites, friends and the platform overlays are handled by FEasySessionSocial.
// These stay here so Blueprints keep calling one subsystem.

bool UEasySessionSubsystem::SendSessionInviteToFriend(const FEasySessionFriend& Friend)
{
	return Social.IsValid() && Social->SendInviteToFriend(Friend);
}

bool UEasySessionSubsystem::ShowInviteUI()
{
	return Social.IsValid() && Social->ShowInviteUI();
}

// Two entry points for one overlay call: Blueprint cannot hold a unique id, so the
// caller passes whichever struct it has and the id is taken out here.
bool UEasySessionSubsystem::ShowProfileUI(const FEasySessionFriend& Friend)
{
	return Social.IsValid() && Social->ShowProfileUI(Friend.NativeId.GetUniqueNetId());
}

bool UEasySessionSubsystem::ShowProfileUIForPlayer(const FEasySessionPlayerInfo& Player)
{
	return Social.IsValid() && Social->ShowProfileUI(Player.PlayerId.GetUniqueNetId());
}

void UEasySessionSubsystem::FindEasyFriendSessions(FEasyFriendSessionsCompleteDelegate OnComplete)
{
	if (!Social.IsValid())
	{
		OnComplete.ExecuteIfBound(EEasySessionResult::NoOnlineSubsystem, TEXT("The session subsystem is shutting down."), {});
		return;
	}

	Social->FindFriendSessions(MoveTemp(OnComplete));
}

void UEasySessionSubsystem::ReadFriends(FEasyFriendsCompleteDelegate OnComplete)
{
	if (!Social.IsValid())
	{
		OnComplete.ExecuteIfBound(EEasySessionResult::NoOnlineSubsystem, TEXT("The session subsystem is shutting down."), {});
		return;
	}

	Social->ReadFriends(MoveTemp(OnComplete));
}

void UEasySessionSubsystem::EnsureStateActor()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr || World->GetNetMode() == NM_Client)
	{
		return;
	}

	if (StateActor.IsValid() && StateActor->GetWorld() == World)
	{
		PushHostSessionState();
		PushReplicatedSessionSettings();
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	StateActor = World->SpawnActor<AEasySessionStateActor>(SpawnParams);
	PushHostSessionState();
	PushReplicatedSessionSettings();
}

void UEasySessionSubsystem::PushHostSessionState()
{
	if (AEasySessionStateActor* Actor = StateActor.Get())
	{
		Actor->SetHostSessionState(GetLocalSessionState());
	}
}

void UEasySessionSubsystem::PushReplicatedSessionSettings()
{
	AEasySessionStateActor* Actor = StateActor.Get();
	if (Actor == nullptr)
	{
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession == nullptr)
	{
		return;
	}

	const FOnlineSessionSettings& Settings = NamedSession->SessionSettings;

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

	Actor->SetReplicatedSessionSettings(Payload);
}

void UEasySessionSubsystem::HandleWorldInitializedActors(const FActorsInitializedParams& Params)
{
	// Every map load (hard or seamless) creates a fresh world, so the host respawns
	// the replicated state actor there and pushes the current state into it again.
	if (Params.World == nullptr || Params.World->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	// The net mode is read from the world being initialized, not from IsNetworkServer:
	// the game instance may still return the previous world when this fires.
	if (Params.World->GetNetMode() != NM_Client && IsSessionAuthority())
	{
		StateActor.Reset();
		EnsureStateActor();
	}
}

void UEasySessionSubsystem::HandleReplicatedHostSessionState(EEasySessionState HostState)
{
	// Record what the host reports; that is all a client does with it. Get Session
	// State returns this value, and the client's own session copy is deliberately
	// left alone: nothing reads its state on a client, and destroying works from
	// any state.
	if (bHasReplicatedHostSessionState && ReplicatedHostSessionState == HostState)
	{
		return;
	}

	ReplicatedHostSessionState = HostState;
	bHasReplicatedHostSessionState = true;
}

void UEasySessionSubsystem::HandleReplicatedSessionSettings(const FEasySessionReplicatedSettings& Settings)
{
	// A default payload means the host has not written one yet; the authority already holds the real values.
	if (!Settings.bValid || IsSessionAuthority())
	{
		return;
	}

	// PostNetInit and the OnRep can both deliver the same payload - apply it once.
	if (AppliedReplicatedSessionSettings == Settings)
	{
		return;
	}
	AppliedReplicatedSessionSettings = Settings;

	// Patch the local session copy, so the regular getters return the host's values without any new API.
	// Without a session copy there is nothing to patch and nothing for a listener to read - skip the event too.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession != nullptr)
	{
		FOnlineSessionSettings& Local = NamedSession->SessionSettings;
		Local.NumPublicConnections = Settings.MaxPlayers;
		Local.bShouldAdvertise = Settings.bShouldAdvertise;
		Local.bAllowJoinInProgress = Settings.bAllowJoinInProgress;
		Local.bAllowInvites = Settings.bAllowInvites;
		Local.Set(EasySession::SettingKey_DisplayName, Settings.SessionDisplayName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		Local.Set(EasySession::SettingKey_JoinCode, Settings.JoinCode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		Local.Set(EasySession::SettingKey_Hidden, Settings.bHidden ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		Local.Set(EasySession::SettingKey_PasswordProtected, Settings.bPasswordProtected ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		Local.Set(EasySession::SettingKey_Region, static_cast<int32>(Settings.Region), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

		// Replace the custom settings wholesale, so a key the host removed disappears here too.
		for (auto It = Local.Settings.CreateIterator(); It; ++It)
		{
			if (!EasySession::IsReservedSettingKey(It.Key()))
			{
				It.RemoveCurrent();
			}
		}
		for (const FEasySessionReplicatedSetting& Custom : Settings.CustomSettings)
		{
			Local.Set(FName(*Custom.Key), Custom.Value, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		}

		OnSessionSettingsChanged.Broadcast();
	}
}


void UEasySessionSubsystem::DestroyEasySessionForEveryone(FText Reason, FEasySessionCompleteDelegate OnComplete)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr || !IsSessionAuthority())
	{
		UE_LOG(LogEasySession, Warning, TEXT("DestroyEasySessionForEveryone can only be called by the server that created the session."));
		OnComplete.ExecuteIfBound(EEasySessionResult::RequiresSessionAuthority, TEXT("Only the game that created the session can destroy it for everyone."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Destroying the session for everyone: %s"), *Reason.ToString());

	// Tell every remote client to leave with the reason before the session is destroyed.
	if (AEasySessionStateActor* Actor = StateActor.Get())
	{
		Actor->MulticastReturnToMenu(Reason);
	}

	DestroyEasySession(FEasySessionCompleteDelegate::CreateWeakLambda(this,
		[this, OnComplete](EEasySessionResult Result, const FString& ErrorMessage)
		{
			ReturnToMenu();
			OnComplete.ExecuteIfBound(Result, ErrorMessage);
		}));
}

void UEasySessionSubsystem::AutoHostDedicatedServerSession()
{
	FEasySessionHostParams HostParams = GetDefault<UEasySessionConfig>()->DedicatedServerHostParams;
	HostParams.HostMode = EEasySessionHostMode::DedicatedServer;
	HostParams.MapName.Empty();

	UE_LOG(LogEasySession, Log, TEXT("Dedicated server detected. Auto hosting session '%s'."), *HostParams.SessionDisplayName);

	CreateEasySession(HostParams, FEasySessionCompleteDelegate::CreateWeakLambda(this,
		[this](EEasySessionResult Result, const FString& /*ErrorMessage*/)
		{
			UE_LOG(LogEasySession, Log, TEXT("Dedicated server auto host finished: %s (IsHost=%d, IsSessionAuthority=%d)"),
				*EasySession::ResultToString(Result), IsHost() ? 1 : 0, IsSessionAuthority() ? 1 : 0);
		}));
}
