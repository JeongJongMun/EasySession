// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionSubsystem.h"

#include "EasyMatchmakingPolicy.h"
#include "EasySession.h"
#include "EasySessionMessages.h"
#include "EasySessionAddress.h"
#include "EasySessionBeaconPort.h"
#include "EasySessionHost.h"
#include "EasySessionMatchmakingRequest.h"
#include "EasySessionCreateRequest.h"
#include "EasySessionDestroyRequest.h"
#include "EasySessionFindRequest.h"
#include "EasySessionJoinRequest.h"
#include "EasySessionLeaveRequest.h"
#include "EasySessionMatchStateRequest.h"
#include "EasySessionOnlineSession.h"
#include "EasySessionParty.h"
#include "EasySessionPlayerComponent.h"
#include "EasySessionRequest.h"
#include "EasySessionUpdateRequest.h"
#include "EasySessionRequestQueue.h"
#include "EasySessionSocial.h"
#include "EasySessionTravel.h"
#include "EasySessionDiagnostics.h"
#include "EasySessionReservations.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
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

	RequestQueue = MakeUnique<FEasySessionRequestQueue>();
	Travel = MakeUnique<FEasySessionTravel>(*this);
	Social = MakeUnique<FEasySessionSocial>(*this);
	BeaconPort = MakeUnique<FEasySessionBeaconPort>();
	Host = MakeUnique<FEasySessionHost>(*this, *BeaconPort);
	Party = MakeUnique<FEasySessionParty>(*this, *BeaconPort);
	RequestContext = MakeUnique<FEasySessionRequestContext>(FEasySessionRequestContext{ *this, *RequestQueue, *Travel, *Host, *Party });

	if (GEngine != nullptr)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UEasySessionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UEasySessionSubsystem::HandleTravelFailure);
	}

	// The session interface may not exist until the world does, so retry until it is.
	InviteBindTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float /*DeltaTime*/)
	{
		if (!GetSessionInterface().IsValid())
		{
			return true;
		}

		Social->BindInviteDelegates();
#if !UE_BUILD_SHIPPING
		// The fixes it prints are for the developer, not the player.
		// Packaged development builds keep it, because that is where an online subsystem that works in the editor and not in a build gets diagnosed.
		EasySessionDiagnostics::LogReport(EasySessionDiagnostics::RunDiagnostics(GetWorld()));
#endif
		InviteBindTickerHandle.Reset();
		return false;
	}), 0.5f);

	// Requests finish on a later tick and travels end inside the engine, so the busy flag is watched here rather than at every call site.
	BusyTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float /*DeltaTime*/)
	{
		RefreshBusyState();
		return true;
	}));
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

	FTSTicker::GetCoreTicker().RemoveTicker(SessionPlayersChangedHandle);
	SessionPlayersChangedHandle.Reset();

	FTSTicker::GetCoreTicker().RemoveTicker(MenuTravelTickerHandle);
	MenuTravelTickerHandle.Reset();

	// Destroying these unbinds everything they registered, tickers included.
	// Reverse creation order, so a collaborator is destroyed before the one it references.
	RequestContext.Reset();
	Party.Reset();
	Host.Reset();
	BeaconPort.Reset();
	Social.Reset();
	Travel.Reset();
	RequestQueue.Reset();

	Super::Deinitialize();
}

void UEasySessionSubsystem::CreateSession(const FEasySessionHostParams& HostParams, FEasySessionCompleteDelegate OnComplete)
{
	Party->CancelRestore();

	if (IsPartyMember())
	{
		UE_LOG(LogEasySession, Warning, TEXT("%s"), EasySession::InPartyMessage);
		OnComplete.ExecuteIfBound(EEasySessionResult::InParty, EasySession::InPartyMessage);
		return;
	}

	EnqueueRequest(MakeShared<FEasySessionCreateRequest>(HostParams, MoveTemp(OnComplete)));
}

void UEasySessionSubsystem::FindSessions(EEasySessionType Type, const FEasySessionSearchParams& SearchParams, FEasySessionFindCompleteDelegate OnComplete)
{
	EnqueueRequest(MakeShared<FEasySessionFindRequest>(SearchParams, MoveTemp(OnComplete), EasySession::ToSessionName(Type)));
}

void UEasySessionSubsystem::JoinSession(const FEasySessionSearchResult& SearchResult, const FString& Password, const FString& AdditionalTravelOptions, FEasySessionCompleteDelegate OnComplete)
{
	Party->CancelRestore();

	// The search result advertises its type. A party result joins the party, and every other result is checked as a game session.
	if (EasySession::GetAdvertisedSessionType(SearchResult.NativeResult.Session.SessionSettings) == NAME_PartySession)
	{
		if (!Password.IsEmpty() || !AdditionalTravelOptions.IsEmpty())
		{
			const TCHAR* Message = TEXT("A party has no password and travels nowhere. Leave Password and Additional Travel Options empty to join one.");
			UE_LOG(LogEasySession, Warning, TEXT("%s"), Message);
			OnComplete.ExecuteIfBound(EEasySessionResult::InvalidParams, Message);
			return;
		}

		EnqueueRequest(MakeShared<FEasySessionJoinRequest>(SearchResult, MoveTemp(OnComplete)));
		return;
	}

	if (IsPartyMember())
	{
		UE_LOG(LogEasySession, Warning, TEXT("%s"), EasySession::InPartyMessage);
		OnComplete.ExecuteIfBound(EEasySessionResult::InParty, EasySession::InPartyMessage);
		return;
	}

	EnqueueRequest(MakeShared<FEasySessionJoinRequest>(SearchResult, Password, AdditionalTravelOptions, MoveTemp(OnComplete)));
}

void UEasySessionSubsystem::StartSession(FEasySessionCompleteDelegate OnComplete)
{
	EnqueueRequest(MakeShared<FEasySessionMatchStateRequest>(FEasySessionRequest::EType::Start, MoveTemp(OnComplete)));
}

void UEasySessionSubsystem::EndSession(FEasySessionCompleteDelegate OnComplete)
{
	EnqueueRequest(MakeShared<FEasySessionMatchStateRequest>(FEasySessionRequest::EType::End, MoveTemp(OnComplete)));
}

void UEasySessionSubsystem::DestroySession(FEasySessionCompleteDelegate OnComplete)
{
	EnqueueRequest(MakeShared<FEasySessionDestroyRequest>(MoveTemp(OnComplete)));
}

void UEasySessionSubsystem::LeaveSession(EEasySessionType Type, FEasySessionCompleteDelegate OnComplete)
{
	if (Type == EEasySessionType::PartySession)
	{
		Party->CancelRestore();
		EnqueueRequest(MakeShared<FEasySessionLeaveRequest>(EEasyPartyLeaveReason::Left, FText::GetEmpty(), MoveTemp(OnComplete)));
		return;
	}

	// A leaving host takes the session with it, and the request tells each client why before the connection closes.
	EnqueueRequest(MakeShared<FEasySessionLeaveRequest>(EasySession::GetHostLeftSessionReason(), MoveTemp(OnComplete)));
}

EEasySessionResult UEasySessionSubsystem::DestroySessionForEveryone(const FText& Reason)
{
	UWorld* World = GetWorld();
	if (World == nullptr || !IsHost())
	{
		UE_LOG(LogEasySession, Warning, TEXT("%s"), EasySession::RequiresHostMessage);
		return EEasySessionResult::RequiresHost;
	}

	EnqueueRequest(MakeShared<FEasySessionLeaveRequest>(Reason, FEasySessionCompleteDelegate()));
	return EEasySessionResult::Success;
}

void UEasySessionSubsystem::UpdateSession(const FEasySessionSettings& NewSettings, FEasySessionCompleteDelegate OnComplete)
{
	EnqueueRequest(MakeShared<FEasySessionUpdateRequest>(NewSettings, MoveTemp(OnComplete)));
}

EEasySessionResult UEasySessionSubsystem::KickPlayer(const FEasySessionPlayerInfo& Player, const FText& Reason)
{
	if (!IsHost())
	{
		return EEasySessionResult::RequiresHost;
	}

	return Host->KickPlayer(Player.PlayerId, Reason) ? EEasySessionResult::Success : EEasySessionResult::InvalidParams;
}

EEasySessionResult UEasySessionSubsystem::SetSessionReady(bool bReady)
{
	// The host adds the component to every PlayerState, the local player's own included.
	const APlayerController* LocalController = GetGameInstance()->GetFirstLocalPlayerController();
	UEasySessionPlayerComponent* Component = IsInSession() && LocalController != nullptr && LocalController->PlayerState != nullptr
		? LocalController->PlayerState->FindComponentByClass<UEasySessionPlayerComponent>()
		: nullptr;
	if (Component == nullptr)
	{
		return EEasySessionResult::NoSessionExists;
	}

	Component->SetReady(bReady);
	return EEasySessionResult::Success;
}

EEasySessionResult UEasySessionSubsystem::ServerTravel(const FString& MapName)
{
	if (MapName.IsEmpty())
	{
		return EEasySessionResult::InvalidParams;
	}

	// UWorld::ServerTravel does not refuse a client.
	// With no game mode it still sets NextURL and returns true, so this entry check is the only guard.
	if (!IsHost())
	{
		UE_LOG(LogEasySession, Warning, TEXT("%s"), EasySession::RequiresHostMessage);
		return EEasySessionResult::RequiresHost;
	}

	// The game mode refuses a map name that is not a valid package name or holds a character it blocks, and logs why.
	if (!Travel->ServerTravelToMap(MapName))
	{
		return EEasySessionResult::InvalidParams;
	}

	Host->OnServerTravelStarted();
	return EEasySessionResult::Success;
}

void UEasySessionSubsystem::StartMatchmaking(const FEasyMatchmakingParams& MatchmakingParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass, FEasySessionCompleteDelegate OnComplete)
{
	if (IsMatchmakingRunning())
	{
		const TCHAR* AlreadyRunningMessage = TEXT("Matchmaking is already running. Cancel it first.");
		UE_LOG(LogEasySession, Warning, TEXT("%s"), AlreadyRunningMessage);
		OnComplete.ExecuteIfBound(EEasySessionResult::MatchmakingAlreadyInProgress, AlreadyRunningMessage);
		return;
	}

	Party->CancelRestore();

	if (IsPartyMember())
	{
		UE_LOG(LogEasySession, Warning, TEXT("%s"), EasySession::InPartyMessage);
		OnComplete.ExecuteIfBound(EEasySessionResult::InParty, EasySession::InPartyMessage);
		return;
	}

	// The host fallback could only fail after the last search pass, so the params are refused before the first one.
	if (MatchmakingParams.bAllowHostFallback && !MatchmakingParams.Host.IsValid())
	{
		UE_LOG(LogEasySession, Warning, TEXT("%s"), EasySession::InvalidHostParamsMessage);
		OnComplete.ExecuteIfBound(EEasySessionResult::InvalidParams, EasySession::InvalidHostParamsMessage);
		return;
	}

	UEasyMatchmakingPolicy* Policy = NewObject<UEasyMatchmakingPolicy>(this, PolicyClass != nullptr ? PolicyClass.Get() : UEasyMatchmakingPolicy::StaticClass());
	EnqueueRequest(MakeShared<FEasySessionMatchmakingRequest>(MatchmakingParams, *Policy, MoveTemp(OnComplete)));

	// Broadcast after the request is queued, so GetActiveMatchmakingPolicy already returns the run's policy here.
	// The run broadcasts every later event itself.
	OnMatchmakingStarted.Broadcast();
}

void UEasySessionSubsystem::CancelMatchmaking()
{
	if (const TSharedPtr<FEasySessionRequest> Matchmaking = RequestQueue->Find(FEasySessionRequest::EType::Matchmaking))
	{
		Matchmaking->Cancel();
	}
}

void UEasySessionSubsystem::CreateParty(const FEasySessionSettings& Settings, FEasySessionCompleteDelegate OnComplete)
{
	Party->CancelRestore();
	EnqueueRequest(MakeShared<FEasySessionCreateRequest>(Settings, MoveTemp(OnComplete)));
}

EEasySessionResult UEasySessionSubsystem::KickPartyMember(const FEasyPartyMemberInfo& Member, const FText& Reason)
{
	return Party->KickMember(Member.PlayerId, Reason);
}

EEasySessionResult UEasySessionSubsystem::SetPartyReady(bool bReady)
{
	return Party->SetReady(bReady);
}

TArray<FEasyPartyMemberInfo> UEasySessionSubsystem::GetPartyMembers() const
{
	return Party->GetMembers();
}

bool UEasySessionSubsystem::IsRestoringParty() const
{
	return Party->IsRestoring();
}

bool UEasySessionSubsystem::IsMatchmakingRunning() const
{
	return RequestQueue->Find(FEasySessionRequest::EType::Matchmaking).IsValid();
}

EEasyMatchmakingState UEasySessionSubsystem::GetMatchmakingState() const
{
	const TSharedPtr<FEasySessionMatchmakingRequest> Matchmaking = FEasySessionMatchmakingRequest::Cast(RequestQueue->Find(FEasySessionRequest::EType::Matchmaking));
	return Matchmaking.IsValid() ? Matchmaking->GetState() : EEasyMatchmakingState::Idle;
}

UEasyMatchmakingPolicy* UEasySessionSubsystem::GetActiveMatchmakingPolicy() const
{
	const TSharedPtr<FEasySessionMatchmakingRequest> Matchmaking = FEasySessionMatchmakingRequest::Cast(RequestQueue->Find(FEasySessionRequest::EType::Matchmaking));
	return Matchmaking.IsValid() ? Matchmaking->GetPolicy() : nullptr;
}

bool UEasySessionSubsystem::IsInSession(EEasySessionType Type) const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	return Sessions.IsValid() && Sessions->GetNamedSession(EasySession::ToSessionName(Type)) != nullptr;
}

EEasySessionState UEasySessionSubsystem::GetSessionState() const
{
	const EEasySessionState LocalState = GetLocalSessionState();

	// Clients report the host's replicated state, because the host decides the session lifecycle.
	// Every player then agrees on it, whenever they joined.
	const UWorld* World = GetWorld();
	if (LocalState != EEasySessionState::NoSession && ReplicatedSessionState.IsSet() &&
		World != nullptr && World->GetNetMode() == NM_Client)
	{
		return ReplicatedSessionState.GetValue();
	}

	return LocalState;
}

FEasySessionSettings UEasySessionSubsystem::GetSessionSettings(EEasySessionType Type) const
{
	FEasySessionSettings Settings;

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(EasySession::ToSessionName(Type)) : nullptr;
	if (NamedSession == nullptr)
	{
		return Settings;
	}

	// Only the creator holds the settings the session was created with, so every player reads them back from the advertised keys.
	Settings.ReadFrom(NamedSession->SessionSettings);

	// The password is not advertised, and this game holds one only for the game session it hosts.
	// Plain text on purpose, because this game already holds it to check players against, and blanking it would leave no way to remove one through Update.
	if (Type == EEasySessionType::GameSession)
	{
		Settings.Password = Host->GetReservations().GetSessionPassword();
		Settings.bFriendsBypassPassword = Host->GetReservations().GetFriendsBypassPassword();
	}

	return Settings;
}

FString UEasySessionSubsystem::GetJoinCode(EEasySessionType Type) const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(EasySession::ToSessionName(Type)) : nullptr;
	if (NamedSession == nullptr)
	{
		return FString();
	}

	FString JoinCode;
	NamedSession->SessionSettings.Get(EasySession::SettingKey_JoinCode, JoinCode);
	return JoinCode;
}

bool UEasySessionSubsystem::IsHost(EEasySessionType Type) const
{
	// The create request sets bHosting when the create completes, because Steam never does, and a joined session keeps the default false.
	// bHosting is a member of the session object, so hosting ends when the session is destroyed, whatever destroyed it.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(EasySession::ToSessionName(Type)) : nullptr;
	return NamedSession != nullptr && NamedSession->bHosting;
}

FString UEasySessionSubsystem::GetDisplayName(EEasySessionType Type) const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(EasySession::ToSessionName(Type)) : nullptr;
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

	// Outside a session the game state still lists the local player, who is no player of any session.
	if (!IsInSession())
	{
		return Infos;
	}

	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (GameState == nullptr)
	{
		return Infos;
	}

	const APlayerController* LocalController = GetGameInstance()->GetFirstLocalPlayerController();
	const APlayerState* LocalPlayerState = LocalController ? LocalController->PlayerState : nullptr;

	// The session owner's id identifies the host player.
	// Ids are compared instead of names, because the engine truncates player names on login (InitNewPlayer).
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

		const UEasySessionPlayerComponent* Component = PlayerState->FindComponentByClass<UEasySessionPlayerComponent>();
		Info.bIsReady = Component != nullptr && Component->IsReady();
		Info.PlayerId = PlayerId;
	}

	return Infos;
}

int32 UEasySessionSubsystem::GetPlayerCount(EEasySessionType Type) const
{
	if (Type == EEasySessionType::PartySession)
	{
		return Party->GetMembers().Num();
	}

	if (!IsInSession())
	{
		return 0;
	}

	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->PlayerArray.Num() : 0;
}

int32 UEasySessionSubsystem::GetMaxPlayers(EEasySessionType Type) const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(EasySession::ToSessionName(Type)) : nullptr;
	return NamedSession ? NamedSession->SessionSettings.NumPublicConnections : 0;
}

bool UEasySessionSubsystem::IsBusy() const
{
	// Travel counts because the level load is the end of the request.
	return RequestQueue->IsBusy() || Travel->IsTraveling();
}

EEasySessionActivity UEasySessionSubsystem::GetActivity() const
{
	if (const TSharedPtr<FEasySessionRequest> BusyRequest = RequestQueue->GetBusyRequest())
	{
		return BusyRequest->GetActivity();
	}

	return Travel->IsTraveling() ? EEasySessionActivity::Traveling : EEasySessionActivity::None;
}

FString UEasySessionSubsystem::GetQueueStatus() const
{
	return RequestQueue->GetStatusText(Travel->IsTraveling());
}

EEasySessionResult UEasySessionSubsystem::SendInviteToFriend(EEasySessionType Type, const FEasySessionFriend& Friend)
{
	return Social->SendInviteToFriend(Friend, EasySession::ToSessionName(Type));
}

EEasySessionResult UEasySessionSubsystem::ShowInviteUI(EEasySessionType Type)
{
	return Social->ShowInviteUI(EasySession::ToSessionName(Type));
}

EEasySessionResult UEasySessionSubsystem::ShowProfileUI(const FEasySessionFriend& Friend)
{
	return Social->ShowProfileUI(Friend.NativeId.GetUniqueNetId());
}

EEasySessionResult UEasySessionSubsystem::ShowProfileUIForPlayer(const FEasySessionPlayerInfo& Player)
{
	return Social->ShowProfileUI(Player.PlayerId.GetUniqueNetId());
}

EEasySessionResult UEasySessionSubsystem::ShowProfileUIForPartyMember(const FEasyPartyMemberInfo& Member)
{
	return Social->ShowProfileUI(Member.PlayerId.GetUniqueNetId());
}

void UEasySessionSubsystem::ReadFriends(FEasyFriendsCompleteDelegate OnComplete)
{
	// The friends list is not read through the session interface, so the read does not wait for session requests.
	Social->ReadFriends(MoveTemp(OnComplete));
}

FEasyDisconnectInfo UEasySessionSubsystem::ConsumePendingDisconnectInfo()
{
	const FEasyDisconnectInfo Info = PendingDisconnectInfo.Get(FEasyDisconnectInfo());
	PendingDisconnectInfo.Reset();
	return Info;
}

void UEasySessionSubsystem::ClearReplicatedSessionState()
{
	ReplicatedSessionState.Reset();

	// A later session can replicate the same settings, and they must still reach its session copy and OnSessionSettingsChanged.
	AppliedReplicatedSessionSettings = FEasySessionReplicatedSettings();
}

void UEasySessionSubsystem::HandleReplicatedSessionState(EEasySessionState HostState)
{
	// Record what the host reports, which is all a client does with it.
	// GetSessionState returns this value.
	// The client's own session copy is left alone on purpose, because nothing reads its state on a client and destroying works from any state.
	ReplicatedSessionState = HostState;

	// The host decides the session state, so a client reports the new one as soon as it arrives.
	RefreshSessionState();
}

void UEasySessionSubsystem::HandleReplicatedSessionSettings(const FEasySessionReplicatedSettings& Settings)
{
	// A default payload means the host has not written one yet.
	// The host already holds the real values.
	if (!Settings.bValid || IsHost())
	{
		return;
	}

	// PostNetInit and the OnRep can both pass the same payload, so it is applied once.
	if (AppliedReplicatedSessionSettings == Settings)
	{
		return;
	}
	AppliedReplicatedSessionSettings = Settings;

	// Write the host's values into the local session copy, so the regular getters return them without any new API.
	// Without a session copy there is nothing to write and nothing for a listener to read, so the event is skipped too.
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

void UEasySessionSubsystem::FollowHost(const FUniqueNetIdRepl& HostId, bool bLANQuery)
{
	// One matchmaking run at a time, and a player in a group has no run of their own to cancel for this.
	if (IsMatchmakingRunning())
	{
		UE_LOG(LogEasySession, Warning, TEXT("Not following host '%s': a matchmaking run is already running."), *HostId.ToString());
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Following host '%s'."), *HostId.ToString());

	// A player in the lobby of a host who moves leaves that session at once and follows from the Game Default Map.
	// The host leaves only after its players did, so this player never loses a session the follow is replacing.
	if (IsInSession())
	{
		LeaveSession();
	}

	UEasyMatchmakingPolicy* Policy = NewObject<UEasyMatchmakingPolicy>(this);
	EnqueueRequest(FEasySessionMatchmakingRequest::MakeFollow(HostId, bLANQuery, *Policy, FEasySessionCompleteDelegate::CreateWeakLambda(this,
		[this](EEasySessionResult Result, const FString& ErrorMessage)
		{
			// No node waits for a follow, so a failure is broadcast.
			if (Result != EEasySessionResult::Success && Result != EEasySessionResult::Canceled)
			{
				OnSessionFailure.Broadcast(FString::Printf(TEXT("Following the host failed: %s"), *ErrorMessage));
			}
		})));

	OnMatchmakingStarted.Broadcast();
}

void UEasySessionSubsystem::HandlePartyEnded(EEasyPartyLeaveReason Reason, const FText& ReasonText)
{
	// A leave of the party already on the queue destroys the party session, and its own reason is the one the player asked for.
	if (RequestQueue->Find(FEasySessionRequest::EType::Leave, NAME_PartySession).IsValid())
	{
		return;
	}

	EnqueueRequest(MakeShared<FEasySessionLeaveRequest>(Reason, ReasonText, FEasySessionCompleteDelegate()));
}

void UEasySessionSubsystem::EnqueueRequest(TSharedRef<FEasySessionRequest> Request)
{
	// A party request names the party session itself, and every other request and its sub-requests pass the game session to the session interface.
	Request->Initialize(*RequestContext, NAME_GameSession);

	// Without the EasySession online session the engine would travel to the menu before a lost session is destroyed, so the request is refused.
	if (Cast<UEasySessionOnlineSession>(GetGameInstance()->GetOnlineSession()) == nullptr)
	{
		UE_LOG(LogEasySession, Error, TEXT("%s"), EasySession::NoEasySessionGameInstanceMessage);
		Request->Refuse(EEasySessionResult::NoEasySessionGameInstance, EasySession::NoEasySessionGameInstanceMessage);
		return;
	}

	RequestQueue->Enqueue(Request);

	// Reported inside this call, so the UI disables its buttons on the same frame as the click.
	RefreshBusyState();
}

void UEasySessionSubsystem::HandleSessionPlayersChanged()
{
	// A component may end while the world is destroyed, after Deinitialize.
	if (!RequestQueue.IsValid() || SessionPlayersChangedHandle.IsValid())
	{
		return;
	}

	// The PlayerState of a player who logs out is still listed while its component ends, so the list is read on the next tick.
	SessionPlayersChangedHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		SessionPlayersChangedHandle.Reset();
		OnSessionPlayersChanged.Broadcast();
		return false;
	}));
}

void UEasySessionSubsystem::HandleDisconnect(UWorld* World, UNetDriver* NetDriver)
{
	// One disconnect can arrive here more than once, for example as a kick and then as the closed connection.
	// Only the first call queues the destroy and the travel.
	if (MenuTravelTickerHandle.IsValid())
	{
		return;
	}

	// A second destroy would run after the first destroyed the session, and fail with NoSessionExists.
	if (IsInSession() && !RequestQueue->Find(FEasySessionRequest::EType::Destroy).IsValid())
	{
		DestroySession(FEasySessionCompleteDelegate());
	}

	MenuTravelWorld = World;
	MenuTravelTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float /*DeltaTime*/)
	{
		if (RequestQueue->Find(FEasySessionRequest::EType::Destroy).IsValid())
		{
			return true;
		}

		MenuTravelTickerHandle.Reset();

		// If the game traveled to another map during the destroy, a menu travel now would replace that map, so it is skipped.
		if (GetWorld() == MenuTravelWorld.Get())
		{
			Travel->TravelToGameDefaultMap();
		}
		return false;
	}));
}

void UEasySessionSubsystem::ReturnToMainMenu(EEasyDisconnectReason Reason, const FText& ReasonText)
{
	RecordDisconnect(Reason, ReasonText);

	// The state actor calls in, and it may outlive Deinitialize while its world is destroyed.
	if (Travel.IsValid())
	{
		Travel->ReturnToMainMenu();
	}
}

bool UEasySessionSubsystem::IsPartyMember() const
{
	// The party follows its host, so only a member who is not the host is held back.
	return IsInSession(EEasySessionType::PartySession) && !IsHost(EEasySessionType::PartySession);
}

IOnlineSessionPtr UEasySessionSubsystem::GetSessionInterface() const
{
	return Online::GetSessionInterface(GetWorld());
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

void UEasySessionSubsystem::RefreshSessionState()
{
	const EEasySessionState State = GetSessionState();
	if (State == LastReportedSessionState)
	{
		return;
	}

	const EEasySessionState OldState = LastReportedSessionState;
	LastReportedSessionState = State;
	OnSessionStateChanged.Broadcast(OldState, State);
}

void UEasySessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// Every net driver reports here, so a beacon query timing out or a replay error would otherwise destroy the session.
	// Only the game connection counts: the world's driver, and the pending one a client uses while still traveling.
	if (NetDriver != nullptr &&
		NetDriver->NetDriverName != NAME_GameNetDriver && NetDriver->NetDriverName != NAME_PendingNetDriver)
	{
		return;
	}

	if (World != nullptr && World != GetWorld())
	{
		return;
	}

	if (World == nullptr)
	{
		// A connection failure has no world and reaches every game instance in the process, so only the one whose pending game owns this net driver handles it.
		const FWorldContext* PendingContext = NetDriver != nullptr ? GEngine->GetWorldContextFromPendingNetGameNetDriver(NetDriver) : nullptr;
		if (PendingContext != nullptr && PendingContext->OwningGameInstance != GetGameInstance())
		{
			return;
		}

		// A connection this plugin did not start, such as the open console command, leaves no session to destroy.
		if (!IsInSession())
		{
			return;
		}
	}

	const FString Reason = FString::Printf(TEXT("%s: %s"), ENetworkFailure::ToString(FailureType), *ErrorString);
	UE_LOG(LogEasySession, Warning, TEXT("Network failure: %s"), *Reason);
	OnSessionFailure.Broadcast(Reason);

	if (IsHost())
	{
		// On the host this fires for a client whose connection died, not for the host's own.
		// The session is still alive, so returning keeps the host from traveling to the menu over another player's disconnect.
		return;
	}

	EEasyDisconnectReason DisconnectReason = EEasyDisconnectReason::ConnectionLost;
	FText ReasonText = NSLOCTEXT("EasySession", "LostConnectionToHost", "Lost connection to the host.");

	// Only these two types carry a message written for the player.
	// Every other type carries debug text, which belongs in the log.
	const bool bHasMessage =
		(FailureType == ENetworkFailure::PendingConnectionFailure ||
			FailureType == ENetworkFailure::FailureReceived) &&
		!ErrorString.IsEmpty();

	if (bHasMessage)
	{
		// A lost host connection has a message too, so only the RefusalMark PreLogin writes in front means a refusal.
		FString Message = ErrorString;
		if (Message.RemoveFromStart(FEasySessionReservations::RefusalMark, ESearchCase::CaseSensitive))
		{
			DisconnectReason = EEasyDisconnectReason::Rejected;
		}

		ReasonText = FText::FromString(Message);
	}

	// The engine also reports this failure to the online session, which destroys the lost session and travels to the menu, so this only records the reason.
	RecordDisconnect(DisconnectReason, ReasonText);
}

void UEasySessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	if (World != GetWorld())
	{
		return;
	}

	// The travel is over even though no map was loaded.
	// The menu travel that follows the destroy marks itself again.
	Travel->NotifyTravelFailed();

	const FString Reason = FString::Printf(TEXT("%s: %s"), ETravelFailure::ToString(FailureType), *ErrorString);
	UE_LOG(LogEasySession, Warning, TEXT("Travel failure: %s"), *Reason);
	OnSessionFailure.Broadcast(Reason);

	// The engine also reports this failure to the online session, which destroys the lost session and travels to the menu, so this only records the reason.
	RecordDisconnect(EEasyDisconnectReason::TravelFailure, FText::FromString(Reason));
}

void UEasySessionSubsystem::RecordDisconnect(EEasyDisconnectReason Reason, const FText& ReasonText)
{
	// The first reason is kept, because the destroy that follows can fail on its own, for example when the connection drops during it.
	// Only the reason is kept, so a reason the game never read with ConsumePendingDisconnectInfo never stops a later disconnect from destroying the session.
	if (!PendingDisconnectInfo.IsSet())
	{
		FEasyDisconnectInfo& Info = PendingDisconnectInfo.Emplace();
		Info.Reason = Reason;
		Info.ReasonText = ReasonText;
	}
}
