// Copyright (c) 2026 Langerak. Licensed under the MIT License.

// The request protocol of UEasySessionSubsystem: what each queued request does when
// it runs, and how its completion comes back from the online service. One class,
// two files - the same way the engine splits UWorld across World.cpp and
// LevelActor.cpp. Everything else about the subsystem lives in
// EasySessionSubsystem.cpp.

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

void UEasySessionSubsystem::ExecuteActiveRequest()
{
	switch (GetActiveRequest()->Type)
	{
		case FEasySessionRequest::EType::Create:	ExecuteCreate(); break;
		case FEasySessionRequest::EType::Find:		ExecuteFind(); break;
		case FEasySessionRequest::EType::Join:		ExecuteJoin(); break;
		case FEasySessionRequest::EType::Destroy:	ExecuteDestroy(); break;
		case FEasySessionRequest::EType::Update:	ExecuteUpdate(); break;
		case FEasySessionRequest::EType::Start:		ExecuteStart(); break;
		case FEasySessionRequest::EType::End:		ExecuteEnd(); break;
		default: CompleteActiveRequest(EEasySessionResult::UnknownFailure, TEXT("Unknown request type.")); break;
	}
}

void UEasySessionSubsystem::HandleRequestDeadline()
{
	const TSharedPtr<FEasySessionRequest> Request = GetActiveRequest();
	if (!Request.IsValid())
	{
		return;
	}

	// The online service never called back. Fail the request so the queue keeps
	// draining, but remember that a timeout means "the outcome is unknown", not
	// "nothing happened" - the operation may still land afterwards.
	const bool bCouldHaveCreatedSession = Request->CouldHaveCreatedSession();
	const FName TimedOutSessionName = Request->SessionName;
	UE_LOG(LogEasySession, Warning, TEXT("%s request timed out after %.0f seconds without a response from the online service. Continuing with the next request."),
		Request->GetTypeName(), Request->GetElapsedSeconds(FPlatformTime::Seconds()));

	CompleteActiveRequest(EEasySessionResult::Timeout, TEXT("The online service did not respond in time."));

	// A create or join that lands late leaves a session nobody asked for, which
	// would then block the next create with "session already exists". Check for it
	// and clean it up instead of leaving the player stuck.
	if (bCouldHaveCreatedSession)
	{
		const IOnlineSessionPtr Sessions = GetSessionInterface();
		if (Sessions.IsValid() && Sessions->GetNamedSession(TimedOutSessionName) != nullptr)
		{
			UE_LOG(LogEasySession, Warning, TEXT("The timed out request did leave a session behind - destroying it so the next request starts clean."));
			DestroyEasySession();
		}
	}
}

void UEasySessionSubsystem::CompleteActiveRequest(EEasySessionResult Result, const FString& ErrorMessage)
{
	if (!GetActiveRequest().IsValid())
	{
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		switch (GetActiveRequest()->Type)
		{
			case FEasySessionRequest::EType::Create:	Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle); break;
			case FEasySessionRequest::EType::Find:		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle); break;
			case FEasySessionRequest::EType::Join:		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle); break;
			case FEasySessionRequest::EType::Destroy:	Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle); break;
			case FEasySessionRequest::EType::Update:	Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateCompleteHandle); break;
			case FEasySessionRequest::EType::Start:		Sessions->ClearOnStartSessionCompleteDelegate_Handle(StartCompleteHandle); break;
			case FEasySessionRequest::EType::End:		Sessions->ClearOnEndSessionCompleteDelegate_Handle(EndCompleteHandle); break;
			default: break;
		}
	}

	if (Result != EEasySessionResult::Success)
	{
		UE_LOG(LogEasySession, Warning, TEXT("Session operation failed: %s (%s)"), *EasySession::ResultToString(Result), *ErrorMessage);
	}

	const TSharedPtr<FEasySessionRequest> CompletedRequest = RequestQueue->PopActive();

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

		case FEasySessionRequest::EType::Start:
			CompletedRequest->OnComplete.ExecuteIfBound(Result, ErrorMessage);
			OnSessionStarted.Broadcast(Result, ErrorMessage);
			break;

		case FEasySessionRequest::EType::End:
			CompletedRequest->OnComplete.ExecuteIfBound(Result, ErrorMessage);
			OnSessionEnded.Broadcast(Result, ErrorMessage);
			break;

		default:
			break;
	}
}

void UEasySessionSubsystem::ExecuteCreate()
{
	const FEasySessionHostParams& Params = GetActiveRequest()->HostParams;
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

	if (Sessions->GetNamedSession(GetActiveRequest()->SessionName) != nullptr)
	{
		CompleteActiveRequest(EEasySessionResult::SessionAlreadyExists, TEXT("A session already exists. Destroy it first."));
		return;
	}

	const bool bIsDedicated = Params.HostMode == EEasySessionHostMode::DedicatedServer;

	// Dedicated hosting means this process is the server, so asking for it from a
	// game that is not one leaves nobody to open a server: the session would be
	// advertised with no way in. Refuse here instead of letting every client find
	// out through a connection timeout. The world's net mode is what decides, not
	// IsRunningDedicatedServer(), so a dedicated server running under PIE counts.
	const UWorld* CreateWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (bIsDedicated && CreateWorld != nullptr && CreateWorld->GetNetMode() != NM_DedicatedServer)
	{
		CompleteActiveRequest(EEasySessionResult::InvalidParams,
			TEXT("Host Mode is Dedicated Server, but this game is not running as one, so nothing would host the session. Use Listen Server, or run this build as a dedicated server."));
		return;
	}

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
	// Written for both states: an absent key cannot be queried or cleared later.
	// Hidden sessions stay advertised so invites still work; Find filters them out.
	Settings.Set(EasySession::SettingKey_Hidden, Params.bHidden ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// Only the protection flag is advertised - the password itself never leaves the host.
	// Trimmed for the same reason the gate trims what it stores: a whitespace-only
	// password enforces nothing, so it must not advertise protection either.
	Settings.Set(EasySession::SettingKey_PasswordProtected, Params.Password.TrimStartAndEnd().IsEmpty() ? 0 : 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
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

	if (!Sessions->CreateSession(0, GetActiveRequest()->SessionName, Settings))
	{
		CompleteActiveRequest(EEasySessionResult::CreateFailure, TEXT("CreateSession request was rejected by the online subsystem."));
	}
}

void UEasySessionSubsystem::ExecuteFind()
{
	// A new search invalidates the previous one. Dropping the results here rather
	// than when the search completes means nothing can show sessions from the last
	// search while a new one is running - those rooms may already be gone.
	LastSearchResults.Empty();

	const FEasySessionSearchParams& Params = GetActiveRequest()->SearchParams;
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

	if (!ActiveSearch->bIsLanQuery)
	{
		// Steam only searches lobbies when this key is present; without it the query
		// goes to the dedicated server master list and never sees lobby sessions.
		ActiveSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	}

	FindCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UEasySessionSubsystem::HandleFindSessionsComplete));

	UE_LOG(LogEasySession, Log, TEXT("Searching for sessions (MaxResults=%d, LAN=%d)"), Params.MaxResults, ActiveSearch->bIsLanQuery ? 1 : 0);

	if (!Sessions->FindSessions(0, ActiveSearch.ToSharedRef()))
	{
		CompleteActiveRequest(EEasySessionResult::SearchFailure, TEXT("FindSessions request was rejected by the online subsystem."));
	}
}

void UEasySessionSubsystem::ExecuteJoin()
{
	if (!GetActiveRequest()->JoinTarget.IsValid())
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

	if (Sessions->GetNamedSession(GetActiveRequest()->SessionName) != nullptr)
	{
		CompleteActiveRequest(EEasySessionResult::SessionAlreadyExists, TEXT("A session already exists. Destroy it before joining another one."));
		return;
	}

	JoinCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UEasySessionSubsystem::HandleJoinSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Joining session '%s' hosted by '%s'"), *GetActiveRequest()->JoinTarget.SessionDisplayName, *GetActiveRequest()->JoinTarget.HostName);

	if (!Sessions->JoinSession(0, GetActiveRequest()->SessionName, GetActiveRequest()->JoinTarget.NativeResult))
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

	if (Sessions->GetNamedSession(GetActiveRequest()->SessionName) == nullptr)
	{
		CompleteActiveRequest(EEasySessionResult::NoSessionExists, TEXT("There is no session to destroy."));
		return;
	}

	UnregisterLocalPlayerFromSession();

	DestroyCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UEasySessionSubsystem::HandleDestroySessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Destroying session."));

	if (!Sessions->DestroySession(GetActiveRequest()->SessionName))
	{
		CompleteActiveRequest(EEasySessionResult::DestroyFailure, TEXT("DestroySession request was rejected by the online subsystem."));
	}
}

void UEasySessionSubsystem::ExecuteUpdate()
{
	const FEasySessionHostParams& Params = GetActiveRequest()->HostParams;
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

	const FNamedOnlineSession* NamedSession = Sessions->GetNamedSession(GetActiveRequest()->SessionName);
	if (NamedSession == nullptr)
	{
		CompleteActiveRequest(EEasySessionResult::NoSessionExists, TEXT("There is no session to update."));
		return;
	}

	// Same gate as ExecuteStart, for the same reason.
	if (!IsSessionAuthority())
	{
		CompleteActiveRequest(EEasySessionResult::RequiresSessionAuthority,
			TEXT("Only the game hosting the session can update it."));
		return;
	}

	FOnlineSessionSettings UpdatedSettings = NamedSession->SessionSettings;
	UpdatedSettings.NumPublicConnections = Params.MaxPlayers;
	UpdatedSettings.bShouldAdvertise = Params.bShouldAdvertise;
	UpdatedSettings.bAllowJoinInProgress = Params.bAllowJoinInProgress;
	UpdatedSettings.bAllowInvites = !NamedSession->SessionSettings.bIsDedicated && Params.bAllowInvites;
	UpdatedSettings.Set(EasySession::SettingKey_DisplayName, Params.SessionDisplayName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	UpdatedSettings.Set(EasySession::SettingKey_Hidden, Params.bHidden ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// The advertised flag and the password ServerGate checks arriving players against
	// have to move together. The flag goes out here; the gate's copy is set in
	// HandleUpdateSessionComplete, once this request is known to have succeeded.
	UpdatedSettings.Set(EasySession::SettingKey_PasswordProtected, Params.Password.TrimStartAndEnd().IsEmpty() ? 0 : 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	for (const TPair<FString, FString>& Custom : Params.CustomSettings)
	{
		UpdatedSettings.Set(FName(*Custom.Key), Custom.Value, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	UpdateCompleteHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(
		FOnUpdateSessionCompleteDelegate::CreateUObject(this, &UEasySessionSubsystem::HandleUpdateSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Updating session."));

	if (!Sessions->UpdateSession(GetActiveRequest()->SessionName, UpdatedSettings, true))
	{
		CompleteActiveRequest(EEasySessionResult::UpdateFailure, TEXT("UpdateSession request was rejected by the online subsystem."));
	}
}

void UEasySessionSubsystem::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (!GetActiveRequest().IsValid() || GetActiveRequest()->Type != FEasySessionRequest::EType::Create || SessionName != GetActiveRequest()->SessionName)
	{
		return;
	}

	const FEasySessionHostParams HostParams = GetActiveRequest()->HostParams;

	if (!bWasSuccessful)
	{
		CompleteActiveRequest(EEasySessionResult::CreateFailure, TEXT("The online subsystem failed to create the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session created successfully."));

	// This process made the session, so it is the one that serves it - on a dedicated
	// server just as much as on a listen server.
	bCreatedActiveSession = true;

	ServerGate->SetSessionCredentials(HostParams.Password.TrimStartAndEnd(), HostParams.bFriendsBypassPassword);
	EnsureStateActor();
	RegisterLocalPlayerInSession();
	CompleteActiveRequest(EEasySessionResult::Success);
	Travel->EnsureHostIsListening(HostParams);
}

void UEasySessionSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	if (!GetActiveRequest().IsValid() || GetActiveRequest()->Type != FEasySessionRequest::EType::Find)
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

	const FEasySessionSearchParams& Params = GetActiveRequest()->SearchParams;
	for (const FOnlineSessionSearchResult& NativeResult : ActiveSearch->SearchResults)
	{
		if (!NativeResult.IsValid())
		{
			continue;
		}

		FEasySessionSearchResult Result = FEasySessionSearchResult::FromNative(NativeResult);

		// Hidden sessions are advertised for invites/direct joins but never listed in searches.
		if (Result.bIsHidden)
		{
			continue;
		}
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
	if (!GetActiveRequest().IsValid() || GetActiveRequest()->Type != FEasySessionRequest::EType::Join || SessionName != GetActiveRequest()->SessionName)
	{
		return;
	}

	const bool bTravelOnSuccess = GetActiveRequest()->bTravelOnSuccess;
	const FString JoinPassword = GetActiveRequest()->JoinPassword;
	const FString JoinTravelOptions = GetActiveRequest()->JoinTravelOptions;

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
	const bool bResolved = Sessions.IsValid() && Sessions->GetResolvedConnectString(GetActiveRequest()->SessionName, ConnectString) && !ConnectString.IsEmpty();

	if (!bResolved || EasySessionAddress::HasZeroPort(ConnectString))
	{
		CompleteActiveRequest(EEasySessionResult::ResolveFailure, FString::Printf(
			TEXT("The host address '%s' is not connectable - the host is not running as a listen server. Make sure the host creates its session with Start Listening enabled or travels to a map with the ?listen option."),
			*ConnectString));

		// Leave the half-joined session so the player can immediately search and join again.
		DestroyEasySession();
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session joined successfully."));

	// Joining settles the question the other way: someone else serves this session.
	bCreatedActiveSession = false;

	RegisterLocalPlayerInSession();
	CompleteActiveRequest(EEasySessionResult::Success);

	if (bTravelOnSuccess)
	{
		Travel->TravelToJoinedSession(ConnectString, JoinPassword, JoinTravelOptions);
	}
}

void UEasySessionSubsystem::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (!GetActiveRequest().IsValid() || GetActiveRequest()->Type != FEasySessionRequest::EType::Destroy || SessionName != GetActiveRequest()->SessionName)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		CompleteActiveRequest(EEasySessionResult::DestroyFailure, TEXT("The online subsystem failed to destroy the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session destroyed successfully."));
	bCreatedActiveSession = false;
	ServerGate->ClearSessionCredentials();

	// The session is gone - drop the replicated state carrier and cache with it.
	if (AEasySessionStateActor* Actor = StateActor.Get())
	{
		Actor->Destroy();
	}
	StateActor.Reset();
	ReplicatedHostSessionState = EEasySessionState::NoSession;
	bHasReplicatedHostSessionState = false;

	CompleteActiveRequest(EEasySessionResult::Success);
}

void UEasySessionSubsystem::HandleUpdateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (!GetActiveRequest().IsValid() || GetActiveRequest()->Type != FEasySessionRequest::EType::Update || SessionName != GetActiveRequest()->SessionName)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		CompleteActiveRequest(EEasySessionResult::UpdateFailure, TEXT("The online subsystem failed to update the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session updated successfully."));

	// Only now, so a rejected update leaves the gate matching what is advertised.
	const FEasySessionHostParams& Params = GetActiveRequest()->HostParams;
	ServerGate->SetSessionCredentials(Params.Password.TrimStartAndEnd(), Params.bFriendsBypassPassword);

	CompleteActiveRequest(EEasySessionResult::Success);
}

void UEasySessionSubsystem::ExecuteStart()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	if (Sessions->GetNamedSession(GetActiveRequest()->SessionName) == nullptr)
	{
		CompleteActiveRequest(EEasySessionResult::NoSessionExists, TEXT("There is no session to start."));
		return;
	}

	// Without this gate the call would still "succeed" - StartSession flips the local
	// session copy - while the real session on the server stays Pending. This check is
	// the only thing standing: BlueprintAuthorityOnly has no runtime effect on a
	// subsystem (only AActor::GetFunctionCallspace enforces it).
	if (!IsSessionAuthority())
	{
		CompleteActiveRequest(EEasySessionResult::RequiresSessionAuthority,
			TEXT("Only the game hosting the session can start the match. Gate this button with Is Easy Session Host so clients do not see it."));
		return;
	}

	StartCompleteHandle = Sessions->AddOnStartSessionCompleteDelegate_Handle(
		FOnStartSessionCompleteDelegate::CreateUObject(this, &UEasySessionSubsystem::HandleStartSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Starting session."));

	if (!Sessions->StartSession(GetActiveRequest()->SessionName))
	{
		CompleteActiveRequest(EEasySessionResult::StateChangeFailure, TEXT("StartSession request was rejected by the online subsystem. The session may already be in progress."));
	}
}

void UEasySessionSubsystem::ExecuteEnd()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
		return;
	}

	if (Sessions->GetNamedSession(GetActiveRequest()->SessionName) == nullptr)
	{
		CompleteActiveRequest(EEasySessionResult::NoSessionExists, TEXT("There is no session to end."));
		return;
	}

	// Same gate as ExecuteStart, for the same reason.
	if (!IsSessionAuthority())
	{
		CompleteActiveRequest(EEasySessionResult::RequiresSessionAuthority,
			TEXT("Only the game hosting the session can end the match. Gate this button with Is Easy Session Host so clients do not see it."));
		return;
	}

	EndCompleteHandle = Sessions->AddOnEndSessionCompleteDelegate_Handle(
		FOnEndSessionCompleteDelegate::CreateUObject(this, &UEasySessionSubsystem::HandleEndSessionComplete));

	UE_LOG(LogEasySession, Log, TEXT("Ending session."));

	if (!Sessions->EndSession(GetActiveRequest()->SessionName))
	{
		CompleteActiveRequest(EEasySessionResult::StateChangeFailure, TEXT("EndSession request was rejected by the online subsystem. The session may not be in progress."));
	}
}

void UEasySessionSubsystem::HandleStartSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (!GetActiveRequest().IsValid() || GetActiveRequest()->Type != FEasySessionRequest::EType::Start || SessionName != GetActiveRequest()->SessionName)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		CompleteActiveRequest(EEasySessionResult::StateChangeFailure, TEXT("The online subsystem failed to start the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session started."));

	// The OSS only changes the local session copy - replicate the new state so
	// every client (present or future) converges on it.
	if (IsSessionAuthority())
	{
		PushHostSessionState();
	}

	CompleteActiveRequest(EEasySessionResult::Success);
}

void UEasySessionSubsystem::HandleEndSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (!GetActiveRequest().IsValid() || GetActiveRequest()->Type != FEasySessionRequest::EType::End || SessionName != GetActiveRequest()->SessionName)
	{
		return;
	}

	if (!bWasSuccessful)
	{
		CompleteActiveRequest(EEasySessionResult::StateChangeFailure, TEXT("The online subsystem failed to end the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session ended."));

	// The OSS only changes the local session copy - replicate the new state so
	// every client (present or future) converges on it.
	if (IsSessionAuthority())
	{
		PushHostSessionState();
	}

	CompleteActiveRequest(EEasySessionResult::Success);
}
