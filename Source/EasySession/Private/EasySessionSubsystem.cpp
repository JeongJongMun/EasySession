// Copyright Langerak. All Rights Reserved.

#include "EasySessionSubsystem.h"

#include "EasyMatchmakingPolicy.h"
#include "EasySession.h"
#include "EasySessionClientComponent.h"
#include "EasySessionSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"

/**
 * A single queued session operation.
 * Requests are executed strictly one at a time by the subsystem.
 */
class FEasySessionRequest
{
public:

	enum class EType : uint8
	{
		Create,
		Find,
		Join,
		Destroy,
		Update
	};

	explicit FEasySessionRequest(EType InType)
		: Type(InType)
	{
	}

	EType Type;
	FEasySessionHostParams HostParams;
	FEasySessionSearchParams SearchParams;
	FEasySessionSearchResult JoinTarget;
	bool bTravelOnSuccess = true;
	FEasySessionCompleteDelegate OnComplete;
	FEasySessionFindCompleteDelegate OnFindComplete;
};

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

	if (GEngine != nullptr)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UEasySessionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UEasySessionSubsystem::HandleTravelFailure);
	}

	PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &UEasySessionSubsystem::HandlePostLogin);

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

	if (PostLoginHandle.IsValid())
	{
		FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
		PostLoginHandle.Reset();
	}

	if (DedicatedAutoHostTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DedicatedAutoHostTickerHandle);
		DedicatedAutoHostTickerHandle.Reset();
	}

	if (ListenCheckTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ListenCheckTickerHandle);
		ListenCheckTickerHandle.Reset();
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle);
		Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateCompleteHandle);
	}

	ActiveMatchmakingPolicy = nullptr;
	ActiveRequest.Reset();
	PendingRequests.Empty();
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

void UEasySessionSubsystem::JoinEasySession(const FEasySessionSearchResult& SearchResult, bool bTravelOnSuccess, FEasySessionCompleteDelegate OnComplete)
{
	TSharedRef<FEasySessionRequest> Request = MakeShared<FEasySessionRequest>(FEasySessionRequest::EType::Join);
	Request->JoinTarget = SearchResult;
	Request->bTravelOnSuccess = bTravelOnSuccess;
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

void UEasySessionSubsystem::StartQuickPlay(const FEasyQuickPlayParams& QuickPlayParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass, FEasySessionCompleteDelegate OnComplete)
{
	if (IsMatchmaking())
	{
		OnComplete.ExecuteIfBound(EEasySessionResult::MatchmakingAlreadyInProgress, TEXT("Matchmaking is already running. Cancel it first."));
		return;
	}

	UEasyMatchmakingPolicy* Policy = NewObject<UEasyMatchmakingPolicy>(this, PolicyClass != nullptr ? PolicyClass.Get() : UEasyMatchmakingPolicy::StaticClass());
	ActiveMatchmakingPolicy = Policy;

	Policy->Start(*this, QuickPlayParams, FEasySessionCompleteDelegate::CreateWeakLambda(this,
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

bool UEasySessionSubsystem::IsHost() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return false;
	}

	const FNamedOnlineSession* NamedSession = Sessions->GetNamedSession(NAME_GameSession);
	return NamedSession != nullptr && NamedSession->bHosting;
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
	return ActiveRequest.IsValid() || PendingRequests.Num() > 0;
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

	FString TravelURL = MapName;
	if (IsHost() && World->GetNetMode() != NM_DedicatedServer && !TravelURL.Contains(TEXT("?listen")))
	{
		TravelURL += TEXT("?listen");
	}

	UE_LOG(LogEasySession, Log, TEXT("ServerTravel to '%s'"), *TravelURL);
	return World->ServerTravel(TravelURL);
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
	PendingRequests.Add(Request);
	if (!ActiveRequest.IsValid())
	{
		ProcessNextRequest();
	}
}

void UEasySessionSubsystem::ProcessNextRequest()
{
	if (ActiveRequest.IsValid() || PendingRequests.IsEmpty())
	{
		return;
	}

	ActiveRequest = PendingRequests[0];
	PendingRequests.RemoveAt(0);

	switch (ActiveRequest->Type)
	{
		case FEasySessionRequest::EType::Create:	ExecuteCreate(); break;
		case FEasySessionRequest::EType::Find:		ExecuteFind(); break;
		case FEasySessionRequest::EType::Join:		ExecuteJoin(); break;
		case FEasySessionRequest::EType::Destroy:	ExecuteDestroy(); break;
		case FEasySessionRequest::EType::Update:	ExecuteUpdate(); break;
		default: CompleteActiveRequest(EEasySessionResult::UnknownFailure, TEXT("Unknown request type.")); break;
	}
}

void UEasySessionSubsystem::CompleteActiveRequest(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (!ActiveRequest.IsValid())
	{
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		switch (ActiveRequest->Type)
		{
			case FEasySessionRequest::EType::Create:	Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle); break;
			case FEasySessionRequest::EType::Find:		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle); break;
			case FEasySessionRequest::EType::Join:		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle); break;
			case FEasySessionRequest::EType::Destroy:	Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle); break;
			case FEasySessionRequest::EType::Update:	Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateCompleteHandle); break;
			default: break;
		}
	}

	if (Result != EEasySessionResult::Success)
	{
		UE_LOG(LogEasySession, Warning, TEXT("Session operation failed: %s (%s)"), *EasySession::ResultToString(Result), *ErrorMessage);
	}

	const TSharedPtr<FEasySessionRequest> CompletedRequest = ActiveRequest;
	ActiveRequest.Reset();

	switch (CompletedRequest->Type)
	{
		case FEasySessionRequest::EType::Create:
			CompletedRequest->OnComplete.ExecuteIfBound(Result, ErrorMessage);
			OnSessionCreated.Broadcast(Result, ErrorMessage);
			break;

		case FEasySessionRequest::EType::Find:
			CompletedRequest->OnFindComplete.ExecuteIfBound(Result, ErrorMessage, LastSearchResults);
			OnSessionsFound.Broadcast(Result, ErrorMessage, LastSearchResults);
			break;

		case FEasySessionRequest::EType::Join:
			CompletedRequest->OnComplete.ExecuteIfBound(Result, ErrorMessage);
			OnSessionJoined.Broadcast(Result, ErrorMessage);
			break;

		case FEasySessionRequest::EType::Destroy:
			CompletedRequest->OnComplete.ExecuteIfBound(Result, ErrorMessage);
			OnSessionDestroyed.Broadcast(Result, ErrorMessage);
			break;

		case FEasySessionRequest::EType::Update:
			CompletedRequest->OnComplete.ExecuteIfBound(Result, ErrorMessage);
			OnSessionUpdated.Broadcast(Result, ErrorMessage);
			break;

		default:
			break;
	}

	// Defer the next request to the next tick so completion callbacks never nest OSS calls.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float DeltaTime)
	{
		ProcessNextRequest();
		return false;
	}));
}

void UEasySessionSubsystem::ExecuteCreate()
{
	const FEasySessionHostParams& Params = ActiveRequest->HostParams;
	if (!Params.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::InvalidParams, TEXT("Host params are invalid."));
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		CompleteActiveRequest(EEasySessionResult::SessionAlreadyExists, TEXT("A session already exists. Destroy it first."));
		return;
	}

	const bool bIsDedicated = Params.HostMode == EEasySessionHostMode::DedicatedServer;

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = Params.MaxPlayers;
	Settings.bIsDedicated = bIsDedicated;
	Settings.bIsLANMatch = Params.bIsLANMatch || ShouldForceLAN();
	Settings.bShouldAdvertise = Params.bShouldAdvertise;
	Settings.bAllowJoinInProgress = Params.bAllowJoinInProgress;
	Settings.bAllowInvites = !bIsDedicated && Params.bAllowInvites;
	Settings.bUsesPresence = !bIsDedicated && !Settings.bIsLANMatch && Params.bUsePresence;
	Settings.bAllowJoinViaPresence = Settings.bUsesPresence;
	Settings.bUseLobbiesIfAvailable = Settings.bUsesPresence;

	Settings.Set(EasySession::SettingKey_DisplayName, Params.SessionDisplayName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	if (!Params.MapName.IsEmpty())
	{
		Settings.Set(SETTING_MAPNAME, Params.MapName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}
	for (const TPair<FString, FString>& Custom : Params.CustomSettings)
	{
		Settings.Set(FName(*Custom.Key), Custom.Value, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	CreateCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UEasySessionSubsystem::HandleCreateSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Creating session '%s' (%s, MaxPlayers=%d, LAN=%d)"),
		*Params.SessionDisplayName,
		bIsDedicated ? TEXT("dedicated") : TEXT("listen"),
		Params.MaxPlayers,
		Settings.bIsLANMatch ? 1 : 0);

	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
	{
		CompleteActiveRequest(EEasySessionResult::CreateFailure, TEXT("CreateSession request was rejected by the online subsystem."));
	}
}

void UEasySessionSubsystem::ExecuteFind()
{
	const FEasySessionSearchParams& Params = ActiveRequest->SearchParams;
	if (!Params.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::InvalidParams, TEXT("Search params are invalid."));
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	ActiveSearch = MakeShared<FOnlineSessionSearch>();
	ActiveSearch->MaxSearchResults = Params.MaxResults;
	ActiveSearch->bIsLanQuery = Params.bLANQuery || ShouldForceLAN();
	ActiveSearch->TimeoutInSeconds = Params.TimeoutSeconds;

	FindCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UEasySessionSubsystem::HandleFindSessionsComplete));

	UE_LOG(LogEasySession, Log, TEXT("Searching for sessions (MaxResults=%d, LAN=%d)"), Params.MaxResults, ActiveSearch->bIsLanQuery ? 1 : 0);

	if (!Sessions->FindSessions(0, ActiveSearch.ToSharedRef()))
	{
		LastSearchResults.Empty();
		CompleteActiveRequest(EEasySessionResult::SearchFailure, TEXT("FindSessions request was rejected by the online subsystem."));
	}
}

void UEasySessionSubsystem::ExecuteJoin()
{
	if (!ActiveRequest->JoinTarget.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::InvalidParams, TEXT("The search result to join is invalid."));
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		CompleteActiveRequest(EEasySessionResult::SessionAlreadyExists, TEXT("A session already exists. Destroy it before joining another one."));
		return;
	}

	JoinCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UEasySessionSubsystem::HandleJoinSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Joining session '%s' hosted by '%s'"), *ActiveRequest->JoinTarget.SessionDisplayName, *ActiveRequest->JoinTarget.HostName);

	if (!Sessions->JoinSession(0, NAME_GameSession, ActiveRequest->JoinTarget.NativeResult))
	{
		CompleteActiveRequest(EEasySessionResult::JoinFailure, TEXT("JoinSession request was rejected by the online subsystem."));
	}
}

void UEasySessionSubsystem::ExecuteDestroy()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	if (Sessions->GetNamedSession(NAME_GameSession) == nullptr)
	{
		CompleteActiveRequest(EEasySessionResult::NoSessionExists, TEXT("There is no session to destroy."));
		return;
	}

	UnregisterLocalPlayerFromSession();

	DestroyCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UEasySessionSubsystem::HandleDestroySessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Destroying session."));

	if (!Sessions->DestroySession(NAME_GameSession))
	{
		CompleteActiveRequest(EEasySessionResult::DestroyFailure, TEXT("DestroySession request was rejected by the online subsystem."));
	}
}

void UEasySessionSubsystem::ExecuteUpdate()
{
	const FEasySessionHostParams& Params = ActiveRequest->HostParams;
	if (!Params.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::InvalidParams, TEXT("Update params are invalid."));
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	const FNamedOnlineSession* NamedSession = Sessions->GetNamedSession(NAME_GameSession);
	if (NamedSession == nullptr)
	{
		CompleteActiveRequest(EEasySessionResult::NoSessionExists, TEXT("There is no session to update."));
		return;
	}

	FOnlineSessionSettings UpdatedSettings = NamedSession->SessionSettings;
	UpdatedSettings.NumPublicConnections = Params.MaxPlayers;
	UpdatedSettings.bShouldAdvertise = Params.bShouldAdvertise;
	UpdatedSettings.bAllowJoinInProgress = Params.bAllowJoinInProgress;
	UpdatedSettings.Set(EasySession::SettingKey_DisplayName, Params.SessionDisplayName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	for (const TPair<FString, FString>& Custom : Params.CustomSettings)
	{
		UpdatedSettings.Set(FName(*Custom.Key), Custom.Value, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	UpdateCompleteHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(
		FOnUpdateSessionCompleteDelegate::CreateUObject(this, &UEasySessionSubsystem::HandleUpdateSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Updating session."));

	if (!Sessions->UpdateSession(NAME_GameSession, UpdatedSettings, true))
	{
		CompleteActiveRequest(EEasySessionResult::UpdateFailure, TEXT("UpdateSession request was rejected by the online subsystem."));
	}
}

void UEasySessionSubsystem::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName != NAME_GameSession || !ActiveRequest.IsValid() || ActiveRequest->Type != FEasySessionRequest::EType::Create)
	{
		return;
	}

	const FEasySessionHostParams HostParams = ActiveRequest->HostParams;

	if (!bWasSuccessful)
	{
		CompleteActiveRequest(EEasySessionResult::CreateFailure, TEXT("The online subsystem failed to create the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session created successfully."));
	RegisterLocalPlayerInSession();
	CompleteActiveRequest(EEasySessionResult::Success);
	EnsureHostIsListening(HostParams);
}

void UEasySessionSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	if (!ActiveRequest.IsValid() || ActiveRequest->Type != FEasySessionRequest::EType::Find)
	{
		return;
	}

	LastSearchResults.Empty();

	if (!bWasSuccessful || !ActiveSearch.IsValid())
	{
		ActiveSearch.Reset();
		CompleteActiveRequest(EEasySessionResult::SearchFailure, TEXT("The online subsystem failed to search for sessions."));
		return;
	}

	const FEasySessionSearchParams& Params = ActiveRequest->SearchParams;
	for (const FOnlineSessionSearchResult& NativeResult : ActiveSearch->SearchResults)
	{
		if (!NativeResult.IsValid())
		{
			continue;
		}

		FEasySessionSearchResult Result = FEasySessionSearchResult::FromNative(NativeResult);

		if (Result.OpenSlots < Params.MinOpenSlots)
		{
			continue;
		}
		if (Params.MaxPingMs > 0 && Result.PingInMs > Params.MaxPingMs)
		{
			continue;
		}

		bool bMatchesCustomSettings = true;
		for (const TPair<FString, FString>& Required : Params.RequiredCustomSettings)
		{
			const FString* FoundValue = Result.CustomSettings.Find(Required.Key);
			if (FoundValue == nullptr || *FoundValue != Required.Value)
			{
				bMatchesCustomSettings = false;
				break;
			}
		}
		if (!bMatchesCustomSettings)
		{
			continue;
		}

		LastSearchResults.Add(MoveTemp(Result));
	}

	ActiveSearch.Reset();

	UE_LOG(LogEasySession, Log, TEXT("Search complete. %d session(s) found after filtering."), LastSearchResults.Num());
	CompleteActiveRequest(EEasySessionResult::Success);
}

void UEasySessionSubsystem::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type JoinResult)
{
	if (SessionName != NAME_GameSession || !ActiveRequest.IsValid() || ActiveRequest->Type != FEasySessionRequest::EType::Join)
	{
		return;
	}

	const bool bTravelOnSuccess = ActiveRequest->bTravelOnSuccess;

	switch (JoinResult)
	{
		case EOnJoinSessionCompleteResult::Success:
			break;

		case EOnJoinSessionCompleteResult::SessionIsFull:
			CompleteActiveRequest(EEasySessionResult::JoinSessionFull, TEXT("The session is full."));
			return;

		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			CompleteActiveRequest(EEasySessionResult::JoinSessionDoesNotExist, TEXT("The session no longer exists."));
			return;

		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
			CompleteActiveRequest(EEasySessionResult::ResolveFailure, TEXT("Could not retrieve the host address."));
			return;

		default:
			CompleteActiveRequest(EEasySessionResult::JoinFailure, TEXT("The online subsystem failed to join the session."));
			return;
	}

	// Resolve the host address before reporting success, so a dead host fails loudly
	// instead of the client hanging on a connection timeout.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	FString ConnectString;
	const bool bResolved = Sessions.IsValid() && Sessions->GetResolvedConnectString(NAME_GameSession, ConnectString) && !ConnectString.IsEmpty();

	if (!bResolved || ConnectString.EndsWith(TEXT(":0")))
	{
		CompleteActiveRequest(EEasySessionResult::ResolveFailure, FString::Printf(
			TEXT("The host address '%s' is not connectable - the host is not running as a listen server. Make sure the host creates its session with Start Listening enabled or travels to a map with the ?listen option."),
			*ConnectString));

		// Leave the half-joined session so the player can immediately search and join again.
		DestroyEasySession();
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session joined successfully."));
	RegisterLocalPlayerInSession();
	CompleteActiveRequest(EEasySessionResult::Success);

	if (bTravelOnSuccess)
	{
		TravelToJoinedSession(ConnectString);
	}
}

void UEasySessionSubsystem::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName != NAME_GameSession || !ActiveRequest.IsValid() || ActiveRequest->Type != FEasySessionRequest::EType::Destroy)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		CompleteActiveRequest(EEasySessionResult::DestroyFailure, TEXT("The online subsystem failed to destroy the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session destroyed successfully."));
	CompleteActiveRequest(EEasySessionResult::Success);
}

void UEasySessionSubsystem::HandleUpdateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName != NAME_GameSession || !ActiveRequest.IsValid() || ActiveRequest->Type != FEasySessionRequest::EType::Update)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		CompleteActiveRequest(EEasySessionResult::UpdateFailure, TEXT("The online subsystem failed to update the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session updated successfully."));
	CompleteActiveRequest(EEasySessionResult::Success);
}

void UEasySessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	const UWorld* OwnWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World != OwnWorld)
	{
		return;
	}

	const FString Reason = FString::Printf(TEXT("%s: %s"), ENetworkFailure::ToString(FailureType), *ErrorString);
	UE_LOG(LogEasySession, Warning, TEXT("Network failure: %s"), *Reason);
	OnSessionFailure.Broadcast(Reason);

	if (IsHost())
	{
		// A failure on the host usually concerns a single client connection - the session
		// itself is still alive, so the host stays where it is.
		return;
	}

	// The connection to the host is gone - record the reason and recover to the menu.
	NotifyDisconnectedFromSession(EEasyDisconnectReason::ConnectionLost, FText::FromString(Reason));
}

void UEasySessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	const UWorld* OwnWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World != OwnWorld)
	{
		return;
	}

	const FString Reason = FString::Printf(TEXT("%s: %s"), ETravelFailure::ToString(FailureType), *ErrorString);
	UE_LOG(LogEasySession, Warning, TEXT("Travel failure: %s"), *Reason);
	OnSessionFailure.Broadcast(Reason);

	NotifyDisconnectedFromSession(EEasyDisconnectReason::TravelFailure, FText::FromString(Reason));
}

void UEasySessionSubsystem::NotifyDisconnectedFromSession(EEasyDisconnectReason Reason, const FText& ReasonText)
{
	// First reason wins: a recovery is already underway and follow-up failures
	// (e.g. the connection dropping while we leave) would overwrite the real cause.
	if (bHasPendingDisconnectInfo)
	{
		return;
	}

	LastDisconnectInfo.Reason = Reason;
	LastDisconnectInfo.ReasonText = ReasonText;
	bHasPendingDisconnectInfo = true;

	const bool bReturnToMenu = GetDefault<UEasySessionSettings>()->bAutoReturnToMenuOnDisconnect;

	if (IsInSession())
	{
		// Clean up the dead session so the player can host or join again right away.
		DestroyEasySession(FEasySessionCompleteDelegate::CreateWeakLambda(this,
			[this, bReturnToMenu](EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/)
			{
				if (bReturnToMenu)
				{
					ReturnToConfiguredMenu();
				}
			}));
	}
	else if (bReturnToMenu)
	{
		ReturnToConfiguredMenu();
	}
}

FEasyDisconnectInfo UEasySessionSubsystem::ConsumeLastDisconnectInfo()
{
	const FEasyDisconnectInfo Info = LastDisconnectInfo;
	LastDisconnectInfo = FEasyDisconnectInfo();
	bHasPendingDisconnectInfo = false;
	return Info;
}

void UEasySessionSubsystem::ReturnToConfiguredMenu()
{
	const FString MenuMap = GetDefault<UEasySessionSettings>()->ReturnToMenuMap;
	if (MenuMap.IsEmpty())
	{
		// No menu map configured - the engine's default-map behavior applies.
		return;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World != nullptr)
	{
		UE_LOG(LogEasySession, Log, TEXT("Returning to the configured menu map: %s"), *MenuMap);
		UGameplayStatics::OpenLevel(World, FName(*MenuMap));
	}
}

void UEasySessionSubsystem::HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	// Fires on the server only (game modes do not exist on clients).
	const UWorld* OwnWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (GameMode == nullptr || NewPlayer == nullptr || GameMode->GetWorld() != OwnWorld)
	{
		return;
	}

	// Attach the RPC carrier so the host can later send this client back to the
	// menu with a reason (EndSessionForEveryone) without a custom PlayerController.
	if (NewPlayer->FindComponentByClass<UEasySessionClientComponent>() == nullptr)
	{
		UEasySessionClientComponent* Component = NewObject<UEasySessionClientComponent>(NewPlayer);
		Component->RegisterComponent();
	}
}

void UEasySessionSubsystem::EndSessionForEveryone(FText Reason)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr || !IsHost())
	{
		UE_LOG(LogEasySession, Warning, TEXT("EndSessionForEveryone can only be called by the session host."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Ending the session for everyone: %s"), *Reason.ToString());

	// Tell every remote client to leave with the reason before the session goes down.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (PlayerController == nullptr || PlayerController->IsLocalController())
		{
			continue;
		}

		if (UEasySessionClientComponent* Component = PlayerController->FindComponentByClass<UEasySessionClientComponent>())
		{
			Component->ClientReturnToMenu(Reason);
		}
	}

	// Give the RPCs a moment to reach the clients, then take the host down as well.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float /*DeltaTime*/)
	{
		DestroyEasySession(FEasySessionCompleteDelegate::CreateWeakLambda(this,
			[this](EEasySessionResult /*Result*/, const FString& /*ErrorMessage*/)
			{
				ReturnToConfiguredMenu();
			}));
		return false;
	}), 0.5f);
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

void UEasySessionSubsystem::EnsureHostIsListening(const FEasySessionHostParams& HostParams)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
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
			OnSessionFailure.Broadcast(TEXT("Failed to start a listen server on the current map."));
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

		ListenCheckTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float DeltaTime)
		{
			ListenCheckTickerHandle.Reset();

			const UWorld* CurrentWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			if (CurrentWorld != nullptr && CurrentWorld->GetNetMode() == NM_Standalone && IsInSession() && IsHost())
			{
				UE_LOG(LogEasySession, Warning, TEXT("The session is advertised but this game is still not a listen server - clients will fail to connect. If you are testing in PIE, disable 'Run Under One Process' or use Standalone Game windows, and make sure the travel map path is valid."));
			}
			return false;
		}), 3.0f);
	}
}

void UEasySessionSubsystem::TravelToOwnSession(const FEasySessionHostParams& HostParams)
{
	if (HostParams.MapName.IsEmpty())
	{
		return;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	FString TravelURL = HostParams.MapName;
	if (HostParams.HostMode == EEasySessionHostMode::ListenServer && HostParams.bStartListening && !TravelURL.Contains(TEXT("?listen")))
	{
		TravelURL += TEXT("?listen");
	}

	UE_LOG(LogEasySession, Log, TEXT("Traveling to session map '%s'"), *TravelURL);
	if (!World->ServerTravel(TravelURL))
	{
		UE_LOG(LogEasySession, Warning, TEXT("ServerTravel to '%s' failed. Check that the map path is valid (e.g. /Game/Maps/Lobby)."), *TravelURL);
		OnSessionFailure.Broadcast(FString::Printf(TEXT("ServerTravel to '%s' failed."), *TravelURL));
	}
}

void UEasySessionSubsystem::TravelToJoinedSession(const FString& ConnectString)
{
	APlayerController* PlayerController = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		UE_LOG(LogEasySession, Warning, TEXT("No local player controller to travel with. Travel aborted."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Traveling to host at '%s'"), *ConnectString);
	PlayerController->ClientTravel(ConnectString, TRAVEL_Absolute);
}

void UEasySessionSubsystem::AutoHostDedicatedServerSession()
{
	FEasySessionHostParams HostParams = GetDefault<UEasySessionSettings>()->DedicatedServerHostParams;
	HostParams.HostMode = EEasySessionHostMode::DedicatedServer;
	HostParams.MapName.Empty();

	UE_LOG(LogEasySession, Log, TEXT("Dedicated server detected. Auto hosting session '%s'."), *HostParams.SessionDisplayName);
	CreateEasySession(HostParams);
}
