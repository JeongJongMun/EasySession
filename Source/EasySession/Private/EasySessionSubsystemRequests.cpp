// Copyright (c) 2026 Langerak. Licensed under the MIT License.

// The request protocol of UEasySessionSubsystem: what each queued request does when
// it runs, and how its completion comes back from the online service. One class,
// two files - the same way the engine splits UWorld across World.cpp and
// LevelActor.cpp. Everything else about the subsystem lives in
// EasySessionSubsystem.cpp.

#include "EasySessionSubsystem.h"

#include "EasyQuickMatchPolicy.h"
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
	// "nothing happened" - the operation may still land afterwards, which is what
	// completing it as abandoned tells CleanupRequest to deal with.
	UE_LOG(LogEasySession, Warning, TEXT("%s request timed out after %.0f seconds without a response from the online service. Continuing with the next request."),
		Request->GetTypeName(), Request->GetElapsedSeconds(FPlatformTime::Seconds()));

	CompleteActiveRequest(EEasySessionResult::Timeout, TEXT("The online service did not respond in time."), /*bAbandoned*/ true);
}

void UEasySessionSubsystem::CompleteActiveRequest(EEasySessionResult Result, const FString& ErrorMessage, bool bAbandoned)
{
	if (!GetActiveRequest().IsValid())
	{
		return;
	}

	if (Result != EEasySessionResult::Success)
	{
		UE_LOG(LogEasySession, Warning, TEXT("Session operation failed: %s (%s)"), *EasySession::ResultToString(Result), *ErrorMessage);
	}

	CleanupRequest(*GetActiveRequest(), bAbandoned);

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

void UEasySessionSubsystem::CleanupRequest(const FEasySessionRequest& Request, bool bAbandoned)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		ActiveSearch.Reset();
		return;
	}

	// Unbind first: an abandoned request tells the online service to stop below,
	// and some of those paths report the operation as finished on the way out.
	switch (Request.Type)
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

	if (Request.Type == FEasySessionRequest::EType::Find)
	{
		// The online service refuses a new search while it thinks one is running. Not
		// queued: a queued cancel would run behind a search that is already waiting.
		if (bAbandoned && ActiveSearch.IsValid() && ActiveSearch->SearchState == EOnlineAsyncTaskState::InProgress)
		{
			UE_LOG(LogEasySession, Warning, TEXT("Cancelling the abandoned search so later searches are not refused."));
			Sessions->CancelFindSessions();
		}

		ActiveSearch.Reset();
	}

	// The join's approval ask may still be waiting for an answer. Stopping it here
	// destroys the beacon client, so a late answer cannot reach a finished request.
	if (Request.Type == FEasySessionRequest::EType::Join)
	{
		JoinApproval->StopClient();
	}

	// A late create leaves a session that would block the next one.
	if (bAbandoned && Request.CouldHaveCreatedSession() && Sessions->GetNamedSession(Request.SessionName) != nullptr)
	{
		UE_LOG(LogEasySession, Warning, TEXT("The abandoned request left a session behind - destroying it so the next request starts clean."));
		DestroyEasySession();
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

	const FOnlineSessionSettings Settings = MakeCreateSettings(Params, bIsDedicated);

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

FOnlineSessionSettings UEasySessionSubsystem::MakeCreateSettings(const FEasySessionHostParams& Params, bool bIsDedicated)
{
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

	// The title a session browser lists. The service's own name field is the host account.
	Settings.Set(EasySession::SettingKey_DisplayName, Params.SessionDisplayName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// The map the host is on, under the engine's own key. Skipped when unnamed.
	if (!Params.MapName.IsEmpty())
	{
		Settings.Set(SETTING_MAPNAME, Params.MapName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	// Whether Find skips this session. Written even when false: an advertised key cannot be deleted later.
	Settings.Set(EasySession::SettingKey_Hidden, Params.bHidden ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// Whether joining needs a password. The joining game reads it back as
	// FEasySessionSearchResult::bPasswordProtected and asks for one before joining.
	// Trimmed like ServerGate's copy: a whitespace-only password enforces nothing.
	Settings.Set(EasySession::SettingKey_PasswordProtected, Params.Password.TrimStartAndEnd().IsEmpty() ? 0 : 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// The port joiners reach the join approval beacon on. Read from config rather than
	// from a running beacon, because none exists yet - one is created per world, after
	// each travel. GetResolvedConnectString reads this key to build the beacon address.
	Settings.Set(SETTING_BEACONPORT, EasySession::GetJoinApprovalBeaconPort(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// Whether this host runs a join approval beacon. Joiners that find the key ask for
	// approval before traveling, and the host itself reads it back after each travel to
	// decide whether the new world needs a beacon of its own.
	Settings.Set(EasySession::SettingKey_JoinApproval, 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	for (const TPair<FString, FString>& Custom : Params.CustomSettings)
	{
		const FName Key(*Custom.Key);
		if (EasySession::IsReservedSettingKey(Key))
		{
			UE_LOG(LogEasySession, Warning, TEXT("Custom Setting '%s' is a key this plugin uses for itself. It was not written."), *Custom.Key);
			continue;
		}

		Settings.Set(Key, Custom.Value, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}

	return Settings;
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
		return;
	}

	// The online service drops a search it cannot start and still reports success,
	// leaving nothing to call back. It marks the one it did accept as in progress,
	// so an untouched state means ours was the one dropped.
	if (ActiveSearch->SearchState != EOnlineAsyncTaskState::InProgress)
	{
		CompleteActiveRequest(EEasySessionResult::SearchFailure,
			TEXT("Another session search is already running, so this one was dropped by the online service."));
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

	// Sessions without the approval key are joined directly - PreLogin still decides,
	// just after the travel instead of before it.
	int32 bJoinApproval = 0;
	if (GetActiveRequest()->JoinTarget.NativeResult.Session.SessionSettings.Get(EasySession::SettingKey_JoinApproval, bJoinApproval) && bJoinApproval != 0)
	{
		RequestJoinApproval();
		return;
	}

	JoinOnlineSession();
}

void UEasySessionSubsystem::RequestJoinApproval()
{
	// Asked before the online service join, so a refusal costs no session slot and no map load.
	JoinApproval->RequestJoinApproval(GetActiveRequest()->JoinTarget, GetActiveRequest()->JoinPassword,
		FEasyJoinApprovalComplete::CreateUObject(this, &UEasySessionSubsystem::HandleJoinApprovalResponse));
}

void UEasySessionSubsystem::HandleJoinApprovalResponse(const FEasyJoinApprovalResponse& Response)
{
	// The request may have timed out and been popped while the beacon was waiting.
	if (!GetActiveRequest().IsValid() || GetActiveRequest()->Type != FEasySessionRequest::EType::Join)
	{
		return;
	}

	switch (Response.Result)
	{
		case EEasyJoinApprovalResult::Approved:
			JoinOnlineSession();
			break;

		case EEasyJoinApprovalResult::Unreachable:
			// Join without asking: PreLogin runs the same ApproveJoin on arrival, 
			// so an unreachable beacon can only delay a refusal, never skip one.
			UE_LOG(LogEasySession, Warning, TEXT("Could not ask the join approval beacon - joining directly. A refusal will now arrive after the travel instead of before it."));
			JoinOnlineSession();
			break;

		case EEasyJoinApprovalResult::WrongPassword:
			CompleteActiveRequest(EEasySessionResult::WrongPassword, Response.ReasonText);
			break;

		default:
			CompleteActiveRequest(EEasySessionResult::JoinRefused, Response.ReasonText);
			break;
	}
}

void UEasySessionSubsystem::JoinOnlineSession()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		CompleteActiveRequest(EEasySessionResult::NoOnlineSubsystem, TEXT("No online subsystem available."));
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
		const FName Key(*Custom.Key);
		if (EasySession::IsReservedSettingKey(Key))
		{
			UE_LOG(LogEasySession, Warning, TEXT("Custom Setting '%s' is a key this plugin uses for itself. It was not written."), *Custom.Key);
			continue;
		}

		UpdatedSettings.Set(Key, Custom.Value, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
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
	const FName CreatedSessionName = GetActiveRequest()->SessionName;

	if (!bWasSuccessful)
	{
		CompleteActiveRequest(EEasySessionResult::CreateFailure, TEXT("The online subsystem failed to create the session."));
		return;
	}

	UE_LOG(LogEasySession, Log, TEXT("Session created successfully."));

	// This process created the session, so it is the session's server - on a dedicated
	// server just as much as on a listen server.
	bCreatedActiveSession = true;

	ServerGate->SetSessionCredentials(HostParams.Password.TrimStartAndEnd(), HostParams.bFriendsBypassPassword);
	EnsureStateActor();
	CompleteActiveRequest(EEasySessionResult::Success);

	if (HostParams.MapName.IsEmpty())
	{
		// The host stays on this map, where they logged in before the session existed - so no login has registered them.
		const IOnlineSessionPtr Sessions = GetSessionInterface();
		const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
		const FUniqueNetIdRepl HostId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();
		if (Sessions.IsValid() && HostId.IsValid() && Sessions->RegisterPlayers(CreatedSessionName, { HostId.GetUniqueNetId().ToSharedRef() }))
		{
			UE_LOG(LogEasySession, Log, TEXT("Registered the hosting player in the session."));
		}

		Travel->ListenOnCurrentMap(HostParams);
		JoinApproval->EnsureHost();
	}
	else
	{
		// The host travels to the session map.
		Travel->TravelToOwnSession(HostParams);
	}
}

void UEasySessionSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	if (!GetActiveRequest().IsValid() || GetActiveRequest()->Type != FEasySessionRequest::EType::Find)
	{
		return;
	}

	// Every search in the process rings this delegate. The online service settles a
	// search's state before ringing, so ours still in progress means this is not it.
	if (ActiveSearch.IsValid() && ActiveSearch->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		UE_LOG(LogEasySession, Verbose, TEXT("Ignoring a search completion that belongs to another search."));
		return;
	}

	LastSearchResults.Empty();

	if (!bWasSuccessful || !ActiveSearch.IsValid())
	{
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

	UE_LOG(LogEasySession, Log, TEXT("Search complete. %d session(s) found after filtering."), LastSearchResults.Num());
	CompleteActiveRequest(EEasySessionResult::Success);
}

void UEasySessionSubsystem::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type JoinResult)
{
	if (!GetActiveRequest().IsValid() || GetActiveRequest()->Type != FEasySessionRequest::EType::Join || SessionName != GetActiveRequest()->SessionName)
	{
		return;
	}

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

		case EOnJoinSessionCompleteResult::AlreadyInSession:
			CompleteActiveRequest(EEasySessionResult::SessionAlreadyExists, TEXT("This player is already in the session. Destroy the current session before joining it again."));
			return;

		// UnknownError, and anything the engine adds to this enum later.
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

	// This process joined the session rather than creating it, so it is not the session's server.
	bCreatedActiveSession = false;

	CompleteActiveRequest(EEasySessionResult::Success);

	Travel->TravelToJoinedSession(ConnectString, JoinPassword, JoinTravelOptions);
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
	JoinApproval->StopHost();

	// The session is gone - destroy the replicated state actor and clear the cached host state.
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
