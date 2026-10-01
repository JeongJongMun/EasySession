// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasyMatchmakingPolicy.h"
#include "EasySessionBeaconPort.h"
#include "EasySessionHost.h"
#include "EasySessionFindRequest.h"
#include "EasySessionJoinRequest.h"
#include "EasySessionMatchmakingRequest.h"
#include "EasySessionParty.h"
#include "EasySessionPartyBeacon.h"
#include "EasySessionRequest.h"
#include "EasySessionRequestQueue.h"
#include "EasySessionReservations.h"
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
	 * Spawn the state actor and the reservation beacon, as the host does when it initializes the game mode of the session's map.
	 * Tests call it after the create, because SkipHostTravel keeps them in the world they started in.
	 */
	static void ArriveInSessionMap(UEasySessionSubsystem& Subsystem)
	{
		Subsystem.Host->SpawnWorldActors();
	}

	/**
	 * The host state AEasySessionStateActor last replicated in.
	 * GetSessionState only returns this on a real client, which a headless test world is not.
	 */
	static EEasySessionState GetReplicatedSessionState(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.ReplicatedSessionState.Get(EEasySessionState::NoSession);
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

	/** The Find request running now, on its own or as a sub-request of matchmaking or the friend session search. Null when none runs. */
	static TSharedPtr<FEasySessionFindRequest> GetRunningFind(const UEasySessionSubsystem& Subsystem)
	{
		for (TSharedPtr<FEasySessionRequest> Request = Subsystem.RequestQueue->GetActiveRequest(); Request.IsValid(); Request = Request->GetRunningSubRequest())
		{
			if (Request->Type == FEasySessionRequest::EType::Find)
			{
				return StaticCastSharedPtr<FEasySessionFindRequest>(Request);
			}
		}
		return nullptr;
	}

	/** The native search object of the running Find request, a sub-request of matchmaking included. Null while no search for sessions runs. */
	static TSharedPtr<FOnlineSessionSearch> GetActiveSearch(const UEasySessionSubsystem& Subsystem)
	{
		const TSharedPtr<FEasySessionFindRequest> FindRequest = GetRunningFind(Subsystem);
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
	 * Take the host side down the way a server travel does, which destroys the state actor and releases the beacon port.
	 * A headless test cannot load a second map, so this stands in for the world change.
	 */
	static void DestroyHostSideActors(UEasySessionSubsystem& Subsystem)
	{
		Subsystem.Host->OnServerTravelStarted();
	}

	/** The password arriving players are actually checked against. */
	static FString GetEnforcedSessionPassword(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.Host.IsValid() ? Subsystem.Host->GetReservations().GetSessionPassword() : FString();
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
		const IOnlineSessionPtr Sessions = Online::GetSessionInterface(Subsystem.GetWorld());
		if (Search.IsValid() && Search->SearchState == EOnlineAsyncTaskState::InProgress && Sessions.IsValid())
		{
			Search->SearchState = EOnlineAsyncTaskState::Failed;
			Sessions->TriggerOnFindSessionsCompleteDelegates(false);
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

	/** Whether the request running now was canceled: it is still the active request with no requester waiting for it. */
	static bool IsActiveRequestCanceled(const UEasySessionSubsystem& Subsystem)
	{
		const TSharedPtr<FEasySessionRequest>& Active = Subsystem.RequestQueue->GetActiveRequest();
		return Active.IsValid() && Active->HasNotified();
	}

	/** The shared beacon port, so a test can register host objects the way a beacon family does. */
	static FEasySessionBeaconPort& GetBeaconPort(UEasySessionSubsystem& Subsystem)
	{
		return *Subsystem.BeaconPort;
	}

	/** The listener the reservation beacon registered on, the plugin's own or the project's. Null while none runs. */
	static AOnlineBeaconHost* GetReservationListener(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.BeaconPort.IsValid() ? Subsystem.BeaconPort->GetListener() : nullptr;
	}

	/** Whether the beacon holds a reservation for this player, which PreLogin reads when the player arrives. */
	static bool PlayerHasReservation(const UEasySessionSubsystem& Subsystem, const FUniqueNetIdRepl& PlayerId)
	{
		const AEasySessionReservationBeaconHost* Beacon = GetReservationBeacon(Subsystem);
		return Beacon != nullptr && PlayerId.IsValid() && Beacon->PlayerHasReservation(*PlayerId.GetUniqueNetId());
	}

	/** The part of a player's logout that removes their reservation, which a headless test cannot drive with a real controller. */
	static void RemovePlayerReservation(UEasySessionSubsystem& Subsystem, const FUniqueNetIdRepl& PlayerId)
	{
		Subsystem.Host->Reservations->RemovePlayerReservation(PlayerId);
	}

	/** Keep a player out of the session, the part of a kick that needs no connected player. */
	static void AddKickedPlayer(UEasySessionSubsystem& Subsystem, const FUniqueNetIdRepl& PlayerId)
	{
		Subsystem.Host->Reservations->AddKickedPlayer(PlayerId);
	}

	/** The beacon host that holds the reservations, or null while no beacon runs. */
	static AEasySessionReservationBeaconHost* GetReservationBeacon(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.Host.IsValid() ? Subsystem.Host->Reservations->BeaconHost.Get() : nullptr;
	}

	/** @return The id of the session this subsystem holds, or empty when it holds none. The join refuses a result carrying this id. */
	static FString GetCurrentSessionIdString(const UEasySessionSubsystem& Subsystem)
	{
		const IOnlineSessionPtr Sessions = Subsystem.GetSessionInterface();
		const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
		return NamedSession != nullptr && NamedSession->SessionInfo.IsValid() ? NamedSession->SessionInfo->GetSessionId().ToString() : FString();
	}

	/**
	 * A joinable search result copied from a session this subsystem currently holds: the game session, or the one SessionName names.
	 * The copy shares the live session info, so its address (port 0 when the host never listened) stays readable after the session is destroyed.
	 * The reservations key is turned off in the copy, so joining it does not wait for a beacon no host runs.
	 */
	static FOnlineSessionSearchResult MakeSearchResultFromCurrentSession(UEasySessionSubsystem& Subsystem, FName SessionName = NAME_GameSession)
	{
		FOnlineSessionSearchResult Result;
		const IOnlineSessionPtr Sessions = Subsystem.GetSessionInterface();
		const FNamedOnlineSession* NamedSession = Sessions.IsValid() ? Sessions->GetNamedSession(SessionName) : nullptr;
		if (NamedSession == nullptr)
		{
			return Result;
		}

		Result.Session = *NamedSession;
		Result.Session.SessionSettings.Set(EasySession::SettingKey_Reservations, 0, EOnlineDataAdvertisementType::ViaOnlineService);

		// Created without a local player, the session may have no owner, and an ownerless result fails the join's validity check.
		if (!Result.Session.OwningUserId.IsValid())
		{
			UWorld* World = Subsystem.GetGameInstance() ? Subsystem.GetGameInstance()->GetWorld() : nullptr;
			const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(World);
			Result.Session.OwningUserId = Identity.IsValid() ? Identity->CreateUniquePlayerId(TEXT("EasySessionTestOwner")) : nullptr;
		}

		return Result;
	}

	/** @return The leader's party beacon, or null outside a party. */
	static AEasySessionPartyBeaconHost* GetPartyBeacon(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.Party->BeaconHost.Get();
	}

	/** Ask the reservations directly whether a player may join. The reservation beacon and PreLogin both call this. */
	static EEasyReservationResult AskApproveJoin(const UEasySessionSubsystem& Subsystem, const FString& Password, const FUniqueNetIdRepl& Requester = FUniqueNetIdRepl(),
		const TArray<FUniqueNetIdRepl>& GroupMembers = TArray<FUniqueNetIdRepl>())
	{
		return Subsystem.Host.IsValid()
			? Subsystem.Host->Reservations->ApproveJoin(Password, Requester, GroupMembers).Result
			: FEasyReservationResponse::NotAnswering().Result;
	}

	/**
	 * Complete the running search with these crafted results, standing in for the online subsystem completing it.
	 * One process cannot find its own LAN session, so filter tests inject what a search would have returned.
	 *
	 * @return Whether there was a running search to complete.
	 */
	static bool DriveFindCompletion(UEasySessionSubsystem& Subsystem, const TArray<FOnlineSessionSearchResult>& Results)
	{
		const TSharedPtr<FEasySessionFindRequest> FindRequest = GetRunningFind(Subsystem);
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

	/** Whether a request of this type waits or runs and has not notified its requester yet, the check the subsystem itself makes. */
	static bool HasRequest(const UEasySessionSubsystem& Subsystem, FEasySessionRequest::EType Type)
	{
		return Subsystem.RequestQueue->Find(Type).IsValid();
	}

	/** How many requests of this type are waiting or running as the active request. */
	static int32 CountRequests(const UEasySessionSubsystem& Subsystem, FEasySessionRequest::EType Type)
	{
		const FEasySessionRequestQueue& Queue = *Subsystem.RequestQueue;
		int32 Count = Queue.ActiveRequest.IsValid() && Queue.ActiveRequest->Type == Type ? 1 : 0;
		for (const TSharedRef<FEasySessionRequest>& Request : Queue.Pending)
		{
			Count += Request->Type == Type ? 1 : 0;
		}
		return Count;
	}

	/**
	 * Approve the running join, standing in for the host's reservation beacon.
	 * The beacon client actor is destroyed first, so its own late response cannot reach the request.
	 *
	 * @return Whether a join was waiting for the beacon.
	 */
	static bool ApproveRunningJoin(UEasySessionSubsystem& Subsystem)
	{
		const TSharedPtr<FEasySessionRequest> Active = Subsystem.RequestQueue->GetActiveRequest();
		if (!Active.IsValid() || Active->Type != FEasySessionRequest::EType::Join)
		{
			return false;
		}

		FEasySessionJoinRequest& Join = static_cast<FEasySessionJoinRequest&>(*Active);
		if (!Join.ReservationClient.IsValid())
		{
			return false;
		}

		Join.DestroyReservationClient();
		FEasyReservationResponse Approved;
		Approved.Result = EEasyReservationResult::Approved;
		Join.HandleReservationResponse(Approved);
		return true;
	}

	/** The running matchmaking request. Null while no matchmaking runs. */
	static TSharedPtr<FEasySessionMatchmakingRequest> GetMatchmakingRequest(const UEasySessionSubsystem& Subsystem)
	{
		return FEasySessionMatchmakingRequest::Cast(Subsystem.RequestQueue->Find(FEasySessionRequest::EType::Matchmaking));
	}

	/**
	 * Feed a finished search into the running matchmaking, standing in for a search pass completing with these results.
	 * Call it while the run waits for its next pass.
	 * The wait is removed, because the pass it waits for is the one fed here.
	 */
	static void DriveMatchmakingSearch(UEasySessionSubsystem& Subsystem, const TArray<FEasySessionSearchResult>& Results)
	{
		const TSharedPtr<FEasySessionMatchmakingRequest> Matchmaking = GetMatchmakingRequest(Subsystem);
		if (!Matchmaking.IsValid())
		{
			return;
		}

		if (Matchmaking->PassDelayTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Matchmaking->PassDelayTickerHandle);
			Matchmaking->PassDelayTickerHandle.Reset();
		}
		Matchmaking->HandleSearchComplete(EEasySessionResult::Success, FString(), Results);
	}

	/** The host params the matchmaking fallback would create its session with. Default params while no matchmaking runs. */
	static FEasySessionHostParams MakeMatchmakingFallbackHostParams(const UEasySessionSubsystem& Subsystem)
	{
		const TSharedPtr<FEasySessionMatchmakingRequest> Matchmaking = GetMatchmakingRequest(Subsystem);
		return Matchmaking.IsValid() ? Matchmaking->MakeFallbackHostParams() : FEasySessionHostParams();
	}

	/** The candidates the running matchmaking tries, in try order. */
	static TArray<FEasySessionSearchResult> GetMatchmakingCandidates(const UEasySessionSubsystem& Subsystem)
	{
		const TSharedPtr<FEasySessionMatchmakingRequest> Matchmaking = GetMatchmakingRequest(Subsystem);
		return Matchmaking.IsValid() ? Matchmaking->Candidates : TArray<FEasySessionSearchResult>();
	}

	/** The sessions a run refuses to retry. A test holds the run, so it can read the list after the run completed. */
	static TSet<FString> GetFailedSessionKeys(const FEasySessionMatchmakingRequest& Run)
	{
		return Run.FailedSessionKeys;
	}

	/** @return The params the run searches and joins with. */
	static const FEasyMatchmakingParams& GetMatchmakingParams(const FEasySessionMatchmakingRequest& Run)
	{
		return Run.Params;
	}

	/** How many candidates of the run's last search pass were joined and failed. */
	static int32 GetFailedJoinCount(const FEasySessionMatchmakingRequest& Run)
	{
		return Run.NextCandidateIndex;
	}
};

#endif // WITH_DEV_AUTOMATION_TESTS
