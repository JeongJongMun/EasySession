// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionSubsystem.h"

#include "EasyMatchmakingPolicy.h"
#include "EasySession.h"
#include "EasySessionMessages.h"
#include "EasySessionAddress.h"
#include "EasySessionBeaconPort.h"
#include "EasySessionHost.h"
#include "EasySessionFriendSessionsRequest.h"
#include "EasySessionReadFriendsRequest.h"
#include "EasySessionMatchmakingRequest.h"
#include "EasySessionCreatePartyRequest.h"
#include "EasySessionCreateRequest.h"
#include "EasySessionDestroyRequest.h"
#include "EasySessionFindRequest.h"
#include "EasySessionJoinPartyRequest.h"
#include "EasySessionJoinRequest.h"
#include "EasySessionLeavePartyRequest.h"
#include "EasySessionMatchStateRequest.h"
#include "EasySessionParty.h"
#include "EasySessionPlayerComponent.h"
#include "EasySessionRequest.h"
#include "EasySessionUpdateRequest.h"
#include "EasySessionRequestQueue.h"
#include "EasySessionSocial.h"
#include "EasySessionTravel.h"
#include "EasySessionDiagnostics.h"
#include "EasySessionReservations.h"
#include "EasySessionConfig.h"
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

void UEasySessionSubsystem::FindSessions(const FEasySessionSearchParams& SearchParams, FEasySessionFindCompleteDelegate OnComplete)
{
	EnqueueRequest(MakeShared<FEasySessionFindRequest>(SearchParams, MoveTemp(OnComplete)));
}

void UEasySessionSubsystem::JoinSession(const FEasySessionSearchResult& SearchResult, const FString& Password, const FString& AdditionalTravelOptions, FEasySessionCompleteDelegate OnComplete)
{
	Party->CancelRestore();

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

void UEasySessionSubsystem::LeaveSession(FEasySessionCompleteDelegate OnComplete)
{
	// A leaving host takes the session with it. Destroying it for everyone tells each client why before the connection closes.
	if (IsSessionAuthority())
	{
		DestroySessionForEveryone(EasySession::GetHostLeftSessionReason(), MoveTemp(OnComplete));
		return;
	}

	DestroySession(FEasySessionCompleteDelegate::CreateWeakLambda(this,
		[this, OnComplete](EEasySessionResult Result, const FString& ErrorMessage)
		{
			// Requested before the completion below, the same order every travel in this plugin uses.
			Travel->ReturnToMenu();
			OnComplete.ExecuteIfBound(Result, ErrorMessage);
		}));
}

void UEasySessionSubsystem::DestroySessionForEveryone(FText Reason, FEasySessionCompleteDelegate OnComplete)
{
	UWorld* World = GetWorld();
	if (World == nullptr || !IsSessionAuthority())
	{
		UE_LOG(LogEasySession, Warning, TEXT("%s"), EasySession::RequiresSessionAuthorityMessage);
		OnComplete.ExecuteIfBound(EEasySessionResult::RequiresSessionAuthority, EasySession::RequiresSessionAuthorityMessage);
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Destroying the session for everyone: %s"), *Reason.ToString());

	// Tell every remote client to leave with the reason before the session is destroyed.
	Host->TellEveryoneToReturnToMenu(Reason);

	DestroySession(FEasySessionCompleteDelegate::CreateWeakLambda(this,
		[this, OnComplete](EEasySessionResult Result, const FString& ErrorMessage)
		{
			Travel->ReturnToMenu();
			OnComplete.ExecuteIfBound(Result, ErrorMessage);
		}));
}

void UEasySessionSubsystem::UpdateSession(const FEasySessionSettings& NewSettings, FEasySessionCompleteDelegate OnComplete)
{
	EnqueueRequest(MakeShared<FEasySessionUpdateRequest>(NewSettings, MoveTemp(OnComplete)));
}

EEasySessionResult UEasySessionSubsystem::KickPlayer(const FEasySessionPlayerInfo& Player, const FText& Reason)
{
	if (!IsSessionAuthority())
	{
		return EEasySessionResult::RequiresSessionAuthority;
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

bool UEasySessionSubsystem::ServerTravel(const FString& MapName)
{
	if (MapName.IsEmpty())
	{
		return false;
	}

	// UWorld::ServerTravel does not refuse a client.
	// With no game mode it still sets NextURL and returns true, so this entry check is the only guard.
	if (!IsSessionAuthority())
	{
		UE_LOG(LogEasySession, Warning, TEXT("%s"), EasySession::RequiresSessionAuthorityMessage);
		return false;
	}

	if (!Travel->ServerTravelToMap(MapName))
	{
		return false;
	}

	Host->OnServerTravelStarted();
	return true;
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

	// Broadcast after the request is queued, so Get Active Easy Matchmaking Policy already returns the run's policy here.
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

void UEasySessionSubsystem::CreateParty(const FEasyPartySettings& PartySettings, FEasySessionCompleteDelegate OnComplete)
{
	Party->CancelRestore();
	EnqueueRequest(MakeShared<FEasySessionCreatePartyRequest>(PartySettings, MoveTemp(OnComplete)), NAME_PartySession);
}

void UEasySessionSubsystem::FindParties(const FEasySessionSearchParams& SearchParams, FEasySessionFindCompleteDelegate OnComplete)
{
	EnqueueRequest(MakeShared<FEasySessionFindRequest>(SearchParams, MoveTemp(OnComplete)), NAME_PartySession);
}

void UEasySessionSubsystem::JoinParty(const FEasySessionSearchResult& SearchResult, FEasySessionCompleteDelegate OnComplete)
{
	Party->CancelRestore();
	EnqueueRequest(MakeShared<FEasySessionJoinPartyRequest>(SearchResult, MoveTemp(OnComplete)), NAME_PartySession);
}

void UEasySessionSubsystem::LeaveParty(FEasySessionCompleteDelegate OnComplete)
{
	Party->CancelRestore();
	EnqueueRequest(MakeShared<FEasySessionLeavePartyRequest>(EEasyPartyLeaveReason::Left, FText::GetEmpty(), MoveTemp(OnComplete)), NAME_PartySession);
}

EEasySessionResult UEasySessionSubsystem::KickPartyMember(const FEasyPartyMemberInfo& Member, const FText& Reason)
{
	return Party->KickMember(Member.PlayerId, Reason);
}

EEasySessionResult UEasySessionSubsystem::SetPartyReady(bool bReady)
{
	return Party->SetReady(bReady);
}

bool UEasySessionSubsystem::IsInParty() const
{
	return Party->IsInParty();
}

bool UEasySessionSubsystem::IsPartyLeader() const
{
	return Party->IsLeader();
}

TArray<FEasyPartyMemberInfo> UEasySessionSubsystem::GetPartyMembers() const
{
	return Party->GetMembers();
}

bool UEasySessionSubsystem::IsRestoringParty() const
{
	return Party->IsRestoring();
}

FEasyPartySettings UEasySessionSubsystem::GetPartySettings() const
{
	FEasyPartySettings PartySettings;

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_PartySession) : nullptr;
	if (NamedSession == nullptr)
	{
		return PartySettings;
	}

	PartySettings.MaxMembers = NamedSession->SessionSettings.NumPublicConnections;

	// Only the leader holds the settings it created the party with, so the privacy is read back from the advertised keys for every member.
	int32 Hidden = 0;
	FString JoinCode;
	NamedSession->SessionSettings.Get(EasySession::SettingKey_Hidden, Hidden);
	NamedSession->SessionSettings.Get(EasySession::SettingKey_JoinCode, JoinCode);
	PartySettings.Privacy = Hidden == 0 ? EEasyPartyPrivacy::Public
		: !JoinCode.IsEmpty() ? EEasyPartyPrivacy::JoinCode
		: EEasyPartyPrivacy::InviteOnly;

	return PartySettings;
}

FString UEasySessionSubsystem::GetPartyJoinCode() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_PartySession) : nullptr;
	if (NamedSession == nullptr)
	{
		return FString();
	}

	FString JoinCode;
	NamedSession->SessionSettings.Get(EasySession::SettingKey_JoinCode, JoinCode);
	return JoinCode;
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

bool UEasySessionSubsystem::IsInSession() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	return Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr;
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

FEasySessionSettings UEasySessionSubsystem::GetSessionSettings() const
{
	FEasySessionSettings Params;

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (NamedSession == nullptr)
	{
		return Params;
	}

	Params.ReadFrom(NamedSession->SessionSettings);

	// Plain text on purpose, because this game already holds the password to check players against.
	// Blanking it here would leave no way to remove one through Update.
	Params.Password = Host->GetReservations().GetSessionPassword();
	Params.bFriendsBypassPassword = Host->GetReservations().GetFriendsBypassPassword();

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

bool UEasySessionSubsystem::IsHost() const
{
	// A dedicated server has the authority but no local player, so it can never be the hosting player.
	const UWorld* World = GetWorld();
	return IsSessionAuthority() && World != nullptr && World->GetNetMode() != NM_DedicatedServer;
}

bool UEasySessionSubsystem::IsSessionAuthority() const
{
	// FEasySessionHost sets bHosting when this process creates the session, and a joined session keeps the default false.
	// bHosting is a member of the session object, so the authority ends when the session is destroyed, whatever destroyed it.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	return NamedSession != nullptr && NamedSession->bHosting;
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

		const UEasySessionPlayerComponent* Component = PlayerState->FindComponentByClass<UEasySessionPlayerComponent>();
		Info.bIsReady = Component != nullptr && Component->IsReady();
		Info.PlayerId = PlayerId;
	}

	return Infos;
}

int32 UEasySessionSubsystem::GetSessionPlayerCount() const
{
	const UWorld* World = GetWorld();
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

EEasySessionResult UEasySessionSubsystem::SendSessionInviteToFriend(const FEasySessionFriend& Friend)
{
	return Social->SendInviteToFriend(Friend, NAME_GameSession);
}

EEasySessionResult UEasySessionSubsystem::ShowInviteUI()
{
	return Social->ShowInviteUI(NAME_GameSession);
}

EEasySessionResult UEasySessionSubsystem::SendPartyInviteToFriend(const FEasySessionFriend& Friend)
{
	if (!IsInParty())
	{
		return EEasySessionResult::NoSessionExists;
	}

	if (!IsPartyLeader())
	{
		return EEasySessionResult::RequiresPartyLeader;
	}

	const EEasySessionResult Result = Social->SendInviteToFriend(Friend, NAME_PartySession);
	if (Result == EEasySessionResult::Success)
	{
		Party->AllowPlayer(Friend.NativeId);
	}
	return Result;
}

EEasySessionResult UEasySessionSubsystem::ShowPartyInviteUI()
{
	if (!IsInParty())
	{
		return EEasySessionResult::NoSessionExists;
	}

	if (!IsPartyLeader())
	{
		return EEasySessionResult::RequiresPartyLeader;
	}

	return Social->ShowInviteUI(NAME_PartySession);
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
	// NULL has no friends list, and the failure is reported inside this call.
	if (!FEasySessionReadFriendsRequest::HasFriendsList(GetWorld()))
	{
		UE_LOG(LogEasySession, Warning, TEXT("%s"), EasySession::NoFriendsListMessage);
		OnComplete.ExecuteIfBound(EEasySessionResult::NotSupportedByService, EasySession::NoFriendsListMessage, {});
		return;
	}

	EnqueueRequest(MakeShared<FEasySessionReadFriendsRequest>(MoveTemp(OnComplete)));
}

void UEasySessionSubsystem::FindFriendSessions(FEasyFriendSessionsCompleteDelegate OnComplete)
{
	if (RequestQueue->Find(FEasySessionRequest::EType::FriendSessions).IsValid())
	{
		const TCHAR* AlreadyRunningMessage = TEXT("A friend session search is already running.");
		UE_LOG(LogEasySession, Warning, TEXT("%s"), AlreadyRunningMessage);
		OnComplete.ExecuteIfBound(EEasySessionResult::FriendSearchAlreadyInProgress, AlreadyRunningMessage, {});
		return;
	}

	// NULL has no friends list, and the failure is reported inside this call.
	if (!FEasySessionReadFriendsRequest::HasFriendsList(GetWorld()))
	{
		UE_LOG(LogEasySession, Warning, TEXT("%s"), EasySession::NoFriendsListMessage);
		OnComplete.ExecuteIfBound(EEasySessionResult::NotSupportedByService, EasySession::NoFriendsListMessage, {});
		return;
	}

	EnqueueRequest(MakeShared<FEasySessionFriendSessionsRequest>(MoveTemp(OnComplete)));
}

void UEasySessionSubsystem::CancelFriendSearch()
{
	if (const TSharedPtr<FEasySessionRequest> FriendSessions = RequestQueue->Find(FEasySessionRequest::EType::FriendSessions))
	{
		FriendSessions->Cancel();
	}
}

FEasyDisconnectInfo UEasySessionSubsystem::ConsumePendingDisconnectInfo()
{
	const FEasyDisconnectInfo Info = PendingDisconnectInfo.Get(FEasyDisconnectInfo());
	PendingDisconnectInfo.Reset();
	return Info;
}

void UEasySessionSubsystem::HandleDisconnect(EEasyDisconnectReason Reason, const FText& ReasonText)
{
	// The state actor calls in, and it may outlive Deinitialize while its world is destroyed.
	if (!RequestQueue.IsValid())
	{
		return;
	}

	// First reason wins, because destroying the session can fail on its own (the connection dropping while we leave).
	// Those later failures would replace the real cause with a symptom.
	// Only the reason is protected.
	// Reading it is optional, so a reason the game never read must never stop a later disconnect from being cleaned up.
	if (!PendingDisconnectInfo.IsSet())
	{
		FEasyDisconnectInfo& Info = PendingDisconnectInfo.Emplace();
		Info.Reason = Reason;
		Info.ReasonText = ReasonText;
	}

	const bool bReturnToMenu = GetDefault<UEasySessionConfig>()->bAutoReturnToMenuOnDisconnect;

	if (IsInSession())
	{
		// A destroy already on the queue empties the session before a second one would run, so a second only adds a NoSessionExists failure.
		// That destroy may come from the game and not return to the menu, so the menu travel is requested here.
		if (RequestQueue->Find(FEasySessionRequest::EType::Destroy).IsValid())
		{
			if (bReturnToMenu)
			{
				Travel->ReturnToMenu();
			}
			return;
		}

		// Clean up the dead session so the player can host or join again right away.
		DestroySession(FEasySessionCompleteDelegate::CreateWeakLambda(this,
			[this, bReturnToMenu](EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/)
			{
				if (bReturnToMenu)
				{
					Travel->ReturnToMenu();
				}
			}));
	}
	else if (bReturnToMenu)
	{
		Travel->ReturnToMenu();
	}
}

void UEasySessionSubsystem::ClearReplicatedSessionState()
{
	ReplicatedSessionState.Reset();
}

void UEasySessionSubsystem::HandleReplicatedSessionState(EEasySessionState HostState)
{
	// Record what the host reports, which is all a client does with it.
	// Get Session State returns this value.
	// The client's own session copy is left alone on purpose, because nothing reads its state on a client and destroying works from any state.
	ReplicatedSessionState = HostState;

	// The host decides the session state, so a client reports the new one as soon as it arrives.
	RefreshSessionState();
}

void UEasySessionSubsystem::HandleReplicatedSessionSettings(const FEasySessionReplicatedSettings& Settings)
{
	// A default payload means the host has not written one yet; the authority already holds the real values.
	if (!Settings.bValid || IsSessionAuthority())
	{
		return;
	}

	// PostNetInit and the OnRep can both deliver the same payload, so it is applied once.
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
	// A leave already on the queue destroys the party session, and its own reason is the one the player asked for.
	if (RequestQueue->Find(FEasySessionRequest::EType::LeaveParty).IsValid())
	{
		return;
	}

	EnqueueRequest(MakeShared<FEasySessionLeavePartyRequest>(Reason, ReasonText, FEasySessionCompleteDelegate()), NAME_PartySession);
}

void UEasySessionSubsystem::EnqueuePartyRequest(TSharedRef<FEasySessionRequest> Request)
{
	EnqueueRequest(Request, NAME_PartySession);
}

void UEasySessionSubsystem::HandleSessionPlayersChanged()
{
	// A component may end while the world is destroyed, after Deinitialize.
	if (!RequestQueue.IsValid() || SessionPlayersChangedHandle.IsValid())
	{
		return;
	}

	// A leaving player's PlayerState is still listed while its component ends, so the list is read on the next tick.
	SessionPlayersChangedHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		SessionPlayersChangedHandle.Reset();
		OnSessionPlayersChanged.Broadcast();
		return false;
	}));
}

bool UEasySessionSubsystem::IsPartyMember() const
{
	// The party follows its leader, so only a member who is not the leader is held back.
	return Party->IsInParty() && !Party->IsLeader();
}

IOnlineSessionPtr UEasySessionSubsystem::GetSessionInterface() const
{
	return Online::GetSessionInterface(GetWorld());
}

void UEasySessionSubsystem::EnqueueRequest(TSharedRef<FEasySessionRequest> Request, FName SessionName)
{
	// Where a request's target session is decided.
	// Every sub-request of the request reads it from the request.
	// Queries and gates are game session only and read the constant.
	Request->Initialize(*RequestContext, SessionName);

	RequestQueue->Enqueue(Request);

	// Reported inside this call, so the UI disables its buttons on the same frame as the click.
	RefreshBusyState();
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

		// A connection this plugin did not start, such as the open console command, leaves no session to clean up.
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
		// On the host this fires for a client whose connection died, not for the host's own.
		// The session is still alive, so returning keeps the host from traveling to the menu over another player's disconnect.
		return;
	}

	EEasyDisconnectReason DisconnectReason = EEasyDisconnectReason::ConnectionLost;
	FText ReasonText = NSLOCTEXT("EasySession", "LostConnectionToHost", "Lost connection to the host.");

	// Only these two types carry a message written for the player. Every other type carries debug text, which belongs in the log.
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

	HandleDisconnect(DisconnectReason, ReasonText);
}

void UEasySessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	if (World != GetWorld())
	{
		return;
	}

	// The travel is over even though no map was loaded.
	// The recovery below may start a new one, which marks itself.
	Travel->NotifyTravelFailed();

	const FString Reason = FString::Printf(TEXT("%s: %s"), ETravelFailure::ToString(FailureType), *ErrorString);
	UE_LOG(LogEasySession, Warning, TEXT("Travel failure: %s"), *Reason);
	OnSessionFailure.Broadcast(Reason);

	// A failed server travel leaves the host's world, session and players untouched. Only the map change failed, which OnSessionFailure just reported.
	if (IsSessionAuthority())
	{
		Host->OnServerTravelFailed();
		return;
	}

	HandleDisconnect(EEasyDisconnectReason::TravelFailure, FText::FromString(Reason));
}
