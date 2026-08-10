// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionSubsystem.h"

#include "EasyMatchmakingPolicy.h"
#include "EasySession.h"
#include "EasySessionAddress.h"
#include "EasySessionRequest.h"
#include "EasySessionRequestQueue.h"
#include "EasySessionServerGate.h"
#include "EasySessionSocial.h"
#include "EasySessionStateActor.h"
#include "EasySessionTravel.h"
#include "EasySessionDiagnostics.h"
#include "EasySessionJoinApproval.h"
#include "EasySessionSettings.h"
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

	// A map load ends a travel. The engine guarantees this fires however the load
	// ends: UEngine::LoadMap broadcasts it from a scope guard's destructor "no matter
	// how we exit", and seamless travel broadcasts it once the new world begins play.
	// That guarantee is what makes it safe to hold state until it arrives.
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UEasySessionSubsystem::HandlePostLoadMap);

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
		EasySessionDiagnostics::RunDiagnostics(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
#endif
		InviteBindTickerHandle.Reset();
		return false;
	}), 0.5f);

	WorldInitializedActorsHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &UEasySessionSubsystem::HandleWorldInitializedActors);

	if (IsRunningDedicatedServer() && GetDefault<UEasySessionSettings>()->bAutoHostOnDedicatedServer)
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

	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
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

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle);
		Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateCompleteHandle);
		Sessions->ClearOnStartSessionCompleteDelegate_Handle(StartCompleteHandle);
		Sessions->ClearOnEndSessionCompleteDelegate_Handle(EndCompleteHandle);
	}

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
	EnqueueRequest(Request);
}

void UEasySessionSubsystem::JoinEasySession(const FEasySessionSearchResult& SearchResult, bool bTravelOnSuccess, const FString& Password, const FString& AdditionalTravelOptions, FEasySessionCompleteDelegate OnComplete)
{
	TSharedRef<FEasySessionRequest> Request = MakeShared<FEasySessionRequest>(FEasySessionRequest::EType::Join);
	Request->JoinTarget = SearchResult;
	Request->bTravelOnSuccess = bTravelOnSuccess;
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

void UEasySessionSubsystem::UpdateEasySession(const FEasySessionHostParams& NewHostParams, FEasySessionCompleteDelegate OnComplete)
{
	TSharedRef<FEasySessionRequest> Request = MakeShared<FEasySessionRequest>(FEasySessionRequest::EType::Update);
	Request->HostParams = NewHostParams;
	Request->OnComplete = MoveTemp(OnComplete);
	EnqueueRequest(Request);
}

void UEasySessionSubsystem::StartQuickMatch(const FEasyQuickMatchParams& QuickMatchParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass, FEasySessionCompleteDelegate OnComplete)
{
	if (IsMatchmaking())
	{
		OnComplete.ExecuteIfBound(EEasySessionResult::MatchmakingAlreadyInProgress, TEXT("Matchmaking is already running. Cancel it first."));
		return;
	}

	// Hosting with no map keeps everyone on the map they are already on, and that
	// travel is what turns the host into a listen server. Without it the session is
	// advertised and nobody can connect. Refuse here instead of letting the players
	// who find that session discover it as a timeout.
	if (QuickMatchParams.bAllowHostFallback && QuickMatchParams.Host.MapName.IsEmpty())
	{
		OnComplete.ExecuteIfBound(EEasySessionResult::InvalidParams,
			TEXT("Quick Match needs Host > Map Name so it has somewhere to host when no session is found. Set it, or turn off Allow Host Fallback to only ever join."));
		return;
	}

	UEasyMatchmakingPolicy* Policy = NewObject<UEasyMatchmakingPolicy>(this, PolicyClass != nullptr ? PolicyClass.Get() : UEasyMatchmakingPolicy::StaticClass());
	ActiveMatchmakingPolicy = Policy;

	Policy->Start(*this, QuickMatchParams, FEasySessionCompleteDelegate::CreateWeakLambda(this,
		[this, UserDelegate = MoveTemp(OnComplete)](EEasySessionResult Result, const FString& ErrorMessage)
		{
			ActiveMatchmakingPolicy = nullptr;
			UserDelegate.ExecuteIfBound(Result, ErrorMessage);
			OnMatchmakingComplete.Broadcast(Result, ErrorMessage);
		}));
}

void UEasySessionSubsystem::CancelMatchmaking()
{
	if (ActiveMatchmakingPolicy != nullptr)
	{
		ActiveMatchmakingPolicy->Cancel();
	}
}

bool UEasySessionSubsystem::IsMatchmaking() const
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

FEasySessionHostParams UEasySessionSubsystem::GetEasySessionHostParams() const
{
	FEasySessionHostParams Params;

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession == nullptr || !IsSessionAuthority())
	{
		return Params;
	}

	const FOnlineSessionSettings& Settings = NamedSession->SessionSettings;
	Params.MaxPlayers = Settings.NumPublicConnections;
	Params.bShouldAdvertise = Settings.bShouldAdvertise;
	Params.bAllowJoinInProgress = Settings.bAllowJoinInProgress;
	Params.bAllowInvites = Settings.bAllowInvites;
	Params.bIsLANMatch = Settings.bIsLANMatch;
	Params.bUsePresence = Settings.bUsesPresence;
	Params.HostMode = Settings.bIsDedicated ? EEasySessionHostMode::DedicatedServer : EEasySessionHostMode::ListenServer;

	for (const TPair<FName, FOnlineSessionSetting>& Setting : Settings.Settings)
	{
		if (Setting.Key == EasySession::SettingKey_DisplayName)
		{
			Params.SessionDisplayName = Setting.Value.Data.ToString();
		}
		else if (Setting.Key == SETTING_MAPNAME)
		{
			Params.MapName = Setting.Value.Data.ToString();
		}
		else if (Setting.Key == EasySession::SettingKey_Hidden)
		{
			int32 Hidden = 0;
			Setting.Value.Data.GetValue(Hidden);
			Params.bHidden = Hidden != 0;
		}
		else if (Setting.Key == EasySession::SettingKey_PasswordProtected)
		{
			// Skipped rather than swept into CustomSettings by the else below. There is
			// nothing to read: the flag is derived from Password, which is filled from
			// ServerGate further down.
		}
		else
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
		Info.NativeId = PlayerId.GetUniqueNetId();
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
	// Matchmaking is included on purpose. Quick Match is a policy state machine that
	// enqueues one request per step, so the queue is briefly empty between steps -
	// reporting "not busy" there would let a UI re-enable its buttons mid-search.
	// Travel is included for the same reason: hosting, joining and starting a match
	// all end in a level load that the player experiences as part of the operation.
	return !RequestQueue->IsIdle() || IsMatchmaking() || Travel->IsTraveling();
}

FString UEasySessionSubsystem::GetQueueStatusDescription() const
{
	return RequestQueue->DescribeStatus(Travel->IsTraveling());
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
	// NextURL and returns true - and BlueprintAuthorityOnly has no runtime effect on a
	// subsystem, so this entry check is the only real gate.
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

	UE_LOG(LogEasySession, Log, TEXT("ServerTravel to '%s'"), *TravelURL);
	if (!World->ServerTravel(TravelURL))
	{
		return false;
	}

	Travel->MarkStarted(TEXT("ServerTravelToMap"));
	return true;
}

void UEasySessionSubsystem::HandlePostLoadMap(UWorld* /*LoadedWorld*/)
{
	Travel->NotifyMapLoaded();
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
	// The one place a request's target session is decided. Everything downstream
	// reads it from the request rather than from a constant.
	Request->SessionName = NAME_GameSession;

	RequestQueue->Enqueue(Request);
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

	const bool bReturnToMenu = GetDefault<UEasySessionSettings>()->bAutoReturnToMenuOnDisconnect;

	if (IsInSession())
	{
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

	// Hand this to the engine instead of opening a map ourselves: it cancels any
	// pending net game, strips ?listen/?LAN from the last URL, drops the net driver
	// and browses to the project's Game Default Map. Doing it by hand would skip
	// that cleanup and add a second "where is the menu" setting next to the
	// engine's own. Repeated calls are harmless - the engine no-ops once it is
	// already browsing to the default map.
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
	return Social.IsValid() && Social->ShowProfileUI(Friend.NativeId);
}

bool UEasySessionSubsystem::ShowProfileUIForPlayer(const FEasySessionPlayerInfo& Player)
{
	return Social.IsValid() && Social->ShowProfileUI(Player.NativeId);
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
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	StateActor = World->SpawnActor<AEasySessionStateActor>(SpawnParams);
	PushHostSessionState();
}

void UEasySessionSubsystem::PushHostSessionState()
{
	if (AEasySessionStateActor* Actor = StateActor.Get())
	{
		Actor->SetHostSessionState(GetLocalSessionState());
	}
}

void UEasySessionSubsystem::HandleWorldInitializedActors(const FActorsInitializedParams& Params)
{
	// Every map load (hard or seamless) creates a fresh world, so the host respawns
	// the replicated state carrier there and re-publishes the current state.
	if (Params.World == nullptr || Params.World->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	// The net mode is read from the world being initialized, not from IsNetworkServer:
	// the game instance may not point at it yet when this fires.
	if (Params.World->GetNetMode() != NM_Client && IsSessionAuthority())
	{
		StateActor.Reset();
		EnsureStateActor();
	}
}

void UEasySessionSubsystem::HandleReplicatedHostSessionState(EEasySessionState HostState)
{
	// Record what the host reports; that is all a client does with it. Get Session
	// State serves this value, and the client's own session copy is deliberately
	// left alone: nothing reads its state on a client, and destroying works from
	// any state.
	if (bHasReplicatedHostSessionState && ReplicatedHostSessionState == HostState)
	{
		return;
	}

	ReplicatedHostSessionState = HostState;
	bHasReplicatedHostSessionState = true;
}


void UEasySessionSubsystem::DestroyEasySessionForEveryone(FText Reason)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr || !IsSessionAuthority())
	{
		UE_LOG(LogEasySession, Warning, TEXT("DestroyEasySessionForEveryone can only be called by the server that created the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Destroying the session for everyone: %s"), *Reason.ToString());

	// Tell every remote client to leave with the reason before the session goes down.
	if (AEasySessionStateActor* Actor = StateActor.Get())
	{
		Actor->MulticastReturnToMenu(Reason);
	}

	// No waiting: the multicast is reliable, so it is already queued on every client
	// connection and the engine flushes those queues while closing the net driver.
	// This mirrors AGameSession::ReturnToMainMenuHost, which notifies the clients and
	// tears the host down in the same frame.
	DestroyEasySession(FEasySessionCompleteDelegate::CreateWeakLambda(this,
		[this](EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/)
		{
			ReturnToMenu();
		}));
}

void UEasySessionSubsystem::RegisterLocalPlayerInSession()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	const FUniqueNetIdRepl PlayerId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();

	if (!Sessions.IsValid() || !PlayerId.IsValid())
	{
		return;
	}

	if (Sessions->RegisterPlayers(NAME_GameSession, { PlayerId.GetUniqueNetId().ToSharedRef() }))
	{
		UE_LOG(LogEasySession, Log, TEXT("Registered local player in the session."));
	}
}

void UEasySessionSubsystem::UnregisterLocalPlayerFromSession()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	const FUniqueNetIdRepl PlayerId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();

	if (!Sessions.IsValid() || !PlayerId.IsValid() || Sessions->GetNamedSession(NAME_GameSession) == nullptr)
	{
		return;
	}

	Sessions->UnregisterPlayers(NAME_GameSession, { PlayerId.GetUniqueNetId().ToSharedRef() });
}

void UEasySessionSubsystem::AutoHostDedicatedServerSession()
{
	FEasySessionHostParams HostParams = GetDefault<UEasySessionSettings>()->DedicatedServerHostParams;
	HostParams.HostMode = EEasySessionHostMode::DedicatedServer;
	HostParams.MapName.Empty();

	UE_LOG(LogEasySession, Log, TEXT("Dedicated server detected. Auto hosting session '%s'."), *HostParams.SessionDisplayName);
	CreateEasySession(HostParams);
}
