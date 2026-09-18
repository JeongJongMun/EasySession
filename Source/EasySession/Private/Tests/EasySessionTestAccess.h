// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasyMatchmakingPolicy.h"
#include "EasySessionBeaconPort.h"
#include "EasySessionHost.h"
#include "EasySessionJoinApproval.h"
#include "EasySessionCreateRequest.h"
#include "EasySessionFindRequest.h"
#include "EasySessionRequest.h"
#include "EasySessionServerGate.h"
#include "EasySessionStateActor.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTravel.h"
#include "GameFramework/OnlineReplStructs.h"
#include "EasySessionTypes.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

/**
 * The subsystem's private state, reached on behalf of the tests.
 *
 * Tests need to read things no game should, such as which of two sources a value came from, or a flag the join path would normally set on its own.
 * Keeping those reads here rather than on the subsystem means the plugin a user installs carries no test API in any build configuration.
 * This is the attorney-client idiom.
 * The subsystem befriends this one class, and this class chooses which private members the tests can read.
 */
class FEasySessionTestAccess
{
public:

	/**
	 * Pretend this process did or did not create the active session, by writing the bHosting flag IsSessionAuthority reads.
	 * Creating normally sets it, and a headless test has no second process to join.
	 * Does nothing while no session exists.
	 */
	static void SetCreatedActiveSession(UEasySessionSubsystem& Subsystem, bool bCreated)
	{
		const IOnlineSessionPtr Sessions = Subsystem.GetSessionInterface();
		if (FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr)
		{
			NamedSession->bHosting = bCreated;
		}
	}

	/**
	 * Make the host's travel to Initial Map Name do nothing.
	 * A headless test world has no player controller to travel with.
	 */
	static void SkipHostTravel(UEasySessionSubsystem& Subsystem)
	{
		Subsystem.Travel->bSkipHostTravel = true;
	}

	/**
	 * Spawn the state actor and the join approval host object, as the host does when it initializes the game mode of the session's map.
	 * Tests call it after the create, because SkipHostTravel keeps them in the world they started in.
	 */
	static void ArriveInSessionMap(UEasySessionSubsystem& Subsystem)
	{
		Subsystem.Host->SpawnWorldActors();
	}

	/** The subsystem's request queue, so a test can register operations without a real matchmaking or friend search. */
	static FEasySessionRequestQueue& GetRequestQueue(UEasySessionSubsystem& Subsystem)
	{
		return *Subsystem.RequestQueue;
	}

	/**
	 * The host state AEasySessionStateActor last replicated in.
	 * GetSessionState only returns this on a real client, which a headless test world is not.
	 */
	static EEasySessionState GetReplicatedHostSessionState(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.ReplicatedHostSessionState;
	}

	/** The settings payload the state actor would replicate to session members. Default (bValid false) while no actor exists. */
	static FEasySessionReplicatedSettings GetStateActorReplicatedSettings(const UEasySessionSubsystem& Subsystem)
	{
		const AEasySessionStateActor* Actor = Subsystem.Host.IsValid() ? Subsystem.Host->StateActor.Get() : nullptr;
		return Actor != nullptr ? Actor->GetReplicatedSessionSettings() : FEasySessionReplicatedSettings();
	}

	/** Give the client apply path a payload, standing in for the state actor's OnRep. */
	static void DriveReplicatedSessionSettings(UEasySessionSubsystem& Subsystem, const FEasySessionReplicatedSettings& Settings)
	{
		Subsystem.HandleReplicatedSessionSettings(Settings);
	}

	/** The native search object of the active Find request. Null while no discovery search runs. */
	static TSharedPtr<FOnlineSessionSearch> GetActiveSearch(const UEasySessionSubsystem& Subsystem)
	{
		const TSharedPtr<FEasySessionFindRequest> FindRequest = FEasySessionFindRequest::Cast(Subsystem.GetActiveRequest());
		return FindRequest.IsValid() ? FindRequest->Search : nullptr;
	}

	/** Whether the subsystem is still holding a search object. */
	static bool HasActiveSearch(const UEasySessionSubsystem& Subsystem)
	{
		return GetActiveSearch(Subsystem).IsValid();
	}

	/** Whether the host's replicated state actor exists. */
	static bool HasStateActor(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.Host.IsValid() && Subsystem.Host->StateActor.IsValid();
	}

	/**
	 * Destroy the state actor and release the beacon port, which is what a travel does to the host side.
	 * A headless test cannot load a second map, so this stands in for the world change.
	 */
	static void DestroyHostSideActors(UEasySessionSubsystem& Subsystem)
	{
		Subsystem.Host->DestroyWorldActors();
		Subsystem.BeaconPort->ReleaseListener();
	}

	/** The password arriving players are actually checked against. */
	static FString GetEnforcedSessionPassword(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.Host.IsValid() ? Subsystem.Host->GetGate().GetSessionPassword() : FString();
	}

	/**
	 * The password-protected flag as it is advertised to searching players.
	 * Read together with the enforced password above, a test can prove the two agree.
	 */
	static bool GetAdvertisedPasswordProtected(const UEasySessionSubsystem& Subsystem)
	{
		const IOnlineSessionPtr Sessions = Subsystem.GetSessionInterface();
		const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
		if (NamedSession == nullptr)
		{
			return false;
		}

		int32 Protected = 0;
		NamedSession->SessionSettings.Get(EasySession::SettingKey_PasswordProtected, Protected);
		return Protected != 0;
	}

	/** The open public slots the session currently advertises to searching players. */
	static int32 GetOpenPublicConnections(const UEasySessionSubsystem& Subsystem)
	{
		const IOnlineSessionPtr Sessions = Subsystem.GetSessionInterface();
		const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
		return NamedSession != nullptr ? NamedSession->NumOpenPublicConnections : -1;
	}

	/** How many players are registered with the session. */
	static int32 GetRegisteredPlayerCount(const UEasySessionSubsystem& Subsystem)
	{
		const IOnlineSessionPtr Sessions = Subsystem.GetSessionInterface();
		const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
		return NamedSession != nullptr ? NamedSession->RegisteredPlayers.Num() : -1;
	}

	/**
	 * The stored type of an advertised session setting.
	 * A test needs the type and not just the value, because rewriting a number as a string leaves the key in place and makes every reader see zero.
	 */
	static EOnlineKeyValuePairDataType::Type GetAdvertisedSettingType(const UEasySessionSubsystem& Subsystem, FName Key)
	{
		const IOnlineSessionPtr Sessions = Subsystem.GetSessionInterface();
		const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
		const FOnlineSessionSetting* Setting = NamedSession != nullptr ? NamedSession->SessionSettings.Settings.Find(Key) : nullptr;
		return Setting != nullptr ? Setting->Data.GetType() : EOnlineKeyValuePairDataType::Empty;
	}

	/** An advertised session setting read as a number, the way the plugin's own readers read it. */
	static int32 GetAdvertisedSettingInt(const UEasySessionSubsystem& Subsystem, FName Key)
	{
		const IOnlineSessionPtr Sessions = Subsystem.GetSessionInterface();
		const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
		if (NamedSession == nullptr)
		{
			return 0;
		}

		int32 Value = 0;
		NamedSession->SessionSettings.Get(Key, Value);
		return Value;
	}

	/**
	 * Mark the running search as failed while the online subsystem still holds it, the state a synchronous search failure leaves behind.
	 *
	 * @return Whether there was a running search to fail.
	 */
	static bool FailActiveSearch(UEasySessionSubsystem& Subsystem)
	{
		const TSharedPtr<FOnlineSessionSearch> Search = GetActiveSearch(Subsystem);
		if (Search.IsValid() && Search->SearchState == EOnlineAsyncTaskState::InProgress)
		{
			Search->SearchState = EOnlineAsyncTaskState::Failed;
			return true;
		}
		return false;
	}

	/**
	 * Make the running search look like an internet one, so canceling it takes the path a Steam search takes.
	 * NULL searches are LAN, which the online subsystem really stops, so that path is otherwise never reached headless.
	 *
	 * @return Whether there was a running search to mark.
	 */
	static bool MarkActiveSearchAsInternet(UEasySessionSubsystem& Subsystem)
	{
		const TSharedPtr<FOnlineSessionSearch> Search = GetActiveSearch(Subsystem);
		if (!Search.IsValid())
		{
			return false;
		}
		Search->bIsLanQuery = false;
		return true;
	}

	/** Whether the request running now was canceled: it keeps the active slot with no requester waiting for it. */
	static bool IsActiveRequestCanceled(const UEasySessionSubsystem& Subsystem)
	{
		const TSharedPtr<FEasySessionRequest>& Active = Subsystem.GetActiveRequest();
		return Active.IsValid() && Active->bCanceled;
	}

	/** The shared beacon port, so a test can register host objects the way a beacon family does. */
	static FEasySessionBeaconPort& GetBeaconPort(UEasySessionSubsystem& Subsystem)
	{
		return *Subsystem.BeaconPort;
	}

	/** The beacon host the join approval registered on, the plugin's own or the project's. Null while none runs. */
	static AOnlineBeaconHost* GetJoinApprovalBeaconHost(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.BeaconPort.IsValid() ? Subsystem.BeaconPort->GetListener() : nullptr;
	}

	/**
	 * A joinable search result copied from the session this subsystem currently holds.
	 * The copy shares the live session info, so its address (port 0 when the host never listened) stays readable after the session is destroyed.
	 * Join approval is turned off in the copy, so joining it does not wait for a beacon no host runs.
	 */
	static FOnlineSessionSearchResult MakeSearchResultFromCurrentSession(UEasySessionSubsystem& Subsystem)
	{
		FOnlineSessionSearchResult Result;
		const IOnlineSessionPtr Sessions = Subsystem.GetSessionInterface();
		const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
		if (NamedSession == nullptr)
		{
			return Result;
		}

		Result.Session = *NamedSession;
		Result.Session.SessionSettings.Set(EasySession::SettingKey_JoinApproval, 0, EOnlineDataAdvertisementType::ViaOnlineService);

		// Created without a local player, the session may have no owner, and an ownerless result fails the join's validity check.
		if (!Result.Session.OwningUserId.IsValid())
		{
			UWorld* World = Subsystem.GetGameInstance() ? Subsystem.GetGameInstance()->GetWorld() : nullptr;
			const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(World);
			Result.Session.OwningUserId = Identity.IsValid() ? Identity->CreateUniquePlayerId(TEXT("EasySessionTestOwner")) : nullptr;
		}

		return Result;
	}

	/** Ask the server gate directly whether a player may join. The approval beacon and PreLogin both call this. */
	static EEasyJoinApprovalResult AskApproveJoin(const UEasySessionSubsystem& Subsystem, const FString& SuppliedPassword)
	{
		FEasyJoinApprovalRequest Request;
		Request.Credential = SuppliedPassword;
		return Subsystem.ApproveJoin(Request, FUniqueNetIdRepl()).Result;
	}

	/**
	 * Run the Cleanup of an abandoned Create request, standing in for the watchdog.
	 * NULL completes creates synchronously, so a create abandoned while running cannot be produced headless.
	 */
	static void CleanupAbandonedCreate(UEasySessionSubsystem& Subsystem)
	{
		const TSharedRef<FEasySessionRequest> Request = MakeShared<FEasySessionCreateRequest>(FEasySessionHostParams(), FEasySessionCompleteDelegate());
		Request->Bind(*Subsystem.RequestContext, NAME_GameSession);
		Request->Cleanup(/*bAbandoned*/ true);
	}

	/**
	 * Complete the running search with these crafted results, standing in for the online subsystem completing it.
	 * One process cannot find its own LAN session, so filter tests inject what a search would have returned.
	 *
	 * @return Whether there was a running search to complete.
	 */
	static bool DriveFindCompletion(UEasySessionSubsystem& Subsystem, const TArray<FOnlineSessionSearchResult>& Results)
	{
		const TSharedPtr<FEasySessionFindRequest> FindRequest = FEasySessionFindRequest::Cast(Subsystem.GetActiveRequest());
		const TSharedPtr<FOnlineSessionSearch> Search = FindRequest.IsValid() ? FindRequest->Search : nullptr;
		if (!Search.IsValid() || Search->SearchState != EOnlineAsyncTaskState::InProgress)
		{
			return false;
		}

		// Release the online subsystem's search first. NULL refuses every later search in the process while it holds one.
		const IOnlineSessionPtr Sessions = Subsystem.GetSessionInterface();
		if (Sessions.IsValid())
		{
			Sessions->CancelFindSessions();
		}

		Search->SearchResults = Results;
		Search->SearchState = EOnlineAsyncTaskState::Done;
		FindRequest->HandleFindSessionsComplete(true);
		return true;
	}

	/** Feed a finished search into the matchmaking policy, standing in for a search pass completing with these results. */
	static void DriveMatchmakingSearch(UEasyMatchmakingPolicy& Policy, const TArray<FEasySessionSearchResult>& Results)
	{
		Policy.HandleSearchComplete(EEasySessionResult::Success, FString(), Results);
	}

	/** The host params the matchmaking fallback would create its session with. */
	static FEasySessionHostParams MakeMatchmakingFallbackHostParams(const UEasyMatchmakingPolicy& Policy)
	{
		return Policy.MakeFallbackHostParams();
	}

	/** The candidates the matchmaking run will try, in try order. */
	static TArray<FEasySessionSearchResult> GetMatchmakingCandidates(const UEasyMatchmakingPolicy& Policy)
	{
		return Policy.Candidates;
	}

	/** The sessions the matchmaking run refuses to retry. */
	static TSet<FString> GetMatchmakingFailedSessionKeys(const UEasyMatchmakingPolicy& Policy)
	{
		return Policy.FailedSessionKeys;
	}
};

#endif // WITH_DEV_AUTOMATION_TESTS
