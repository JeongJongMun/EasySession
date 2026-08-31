// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasyMatchmakingPolicy.h"
#include "EasySessionJoinApproval.h"
#include "EasySessionRequest.h"
#include "EasySessionServerGate.h"
#include "EasySessionStateActor.h"
#include "EasySessionSubsystem.h"
#include "GameFramework/OnlineReplStructs.h"
#include "EasySessionTypes.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

/**
 * The subsystem's private state, reached on behalf of the tests.
 *
 * Tests need to read things no game should, such as which of two sources a value came from, or a flag the join path would normally set on its own.
 * Keeping those reads here rather than on the subsystem means the plugin a user installs carries no test API in any build configuration.
 */
class FEasySessionTestAccess
{
public:

	/**
	 * Pretend this process did or did not create the active session.
	 * Creating or joining normally sets this, and a headless test has no second process to join.
	 */
	static void SetCreatedActiveSession(UEasySessionSubsystem& Subsystem, bool bCreated)
	{
		Subsystem.bCreatedActiveSession = bCreated;
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
		const AEasySessionStateActor* Actor = Subsystem.StateActor.Get();
		return Actor != nullptr ? Actor->GetReplicatedSessionSettings() : FEasySessionReplicatedSettings();
	}

	/** Hand the client apply path a payload, standing in for the state actor's OnRep arriving. */
	static void DriveReplicatedSessionSettings(UEasySessionSubsystem& Subsystem, const FEasySessionReplicatedSettings& Settings)
	{
		Subsystem.HandleReplicatedSessionSettings(Settings);
	}

	/** Whether the subsystem is still holding a search object. */
	static bool HasActiveSearch(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.ActiveSearch.IsValid();
	}

	/** The password arriving players are actually checked against. */
	static FString GetEnforcedSessionPassword(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.ServerGate.IsValid() ? Subsystem.ServerGate->GetSessionPassword() : FString();
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

	/** The open public slots the session currently advertises to searchers. */
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
	 * Mark the running search as failed while the online service still holds it - the shape a synchronous search failure leaves behind.
	 *
	 * @return Whether there was a running search to fail.
	 */
	static bool FailActiveSearch(UEasySessionSubsystem& Subsystem)
	{
		if (Subsystem.ActiveSearch.IsValid() && Subsystem.ActiveSearch->SearchState == EOnlineAsyncTaskState::InProgress)
		{
			Subsystem.ActiveSearch->SearchState = EOnlineAsyncTaskState::Failed;
			return true;
		}
		return false;
	}

	/** The beacon host the join approval registered on - the plugin's own or the project's. Null while none runs. */
	static AOnlineBeaconHost* GetJoinApprovalBeaconHost(const UEasySessionSubsystem& Subsystem)
	{
		return Subsystem.JoinApproval.IsValid() ? Subsystem.JoinApproval->GetBeaconHost() : nullptr;
	}

	/**
	 * A joinable search result copied from the session this subsystem currently holds.
	 * The copy shares the live session info, so its address - port 0 when the host never listened - stays readable after the session is destroyed.
	 * Join approval is turned off in the copy, so joining it does not wait on a beacon nobody hosts.
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

		// Created without a local player, the session may have no owner - and an ownerless result fails the join's validity check.
		if (!Result.Session.OwningUserId.IsValid())
		{
			UWorld* World = Subsystem.GetGameInstance() ? Subsystem.GetGameInstance()->GetWorld() : nullptr;
			const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(World);
			Result.Session.OwningUserId = Identity.IsValid() ? Identity->CreateUniquePlayerId(TEXT("EasySessionTestOwner")) : nullptr;
		}

		return Result;
	}

	/** Ask the server gate the join question directly - the approval beacon and PreLogin both route into this same call. */
	static EEasyJoinApprovalResult AskApproveJoin(const UEasySessionSubsystem& Subsystem, const FString& SuppliedPassword)
	{
		FString Reason;
		return Subsystem.ServerGate.IsValid()
			? Subsystem.ServerGate->ApproveJoin(FUniqueNetIdRepl(), SuppliedPassword, Reason)
			: EEasyJoinApprovalResult::Refused;
	}

	/**
	 * Run the abandoned-request cleanup for a request of this type, standing in for the watchdog arriving there.
	 * NULL completes creates synchronously, so a create genuinely abandoned mid-flight cannot be produced headless.
	 */
	static void CleanupAsAbandoned(UEasySessionSubsystem& Subsystem, FEasySessionRequest::EType Type)
	{
		FEasySessionRequest Request(Type);
		Request.SessionName = NAME_GameSession;
		Subsystem.CleanupRequest(Request, /*bAbandoned*/ true);
	}

	/**
	 * Complete the running search with these crafted results, standing in for the online service answering.
	 * One process cannot find its own LAN session, so filter tests inject what a search would have returned.
	 *
	 * @return Whether there was a running search to complete.
	 */
	static bool DriveFindCompletion(UEasySessionSubsystem& Subsystem, const TArray<FOnlineSessionSearchResult>& Results)
	{
		if (!Subsystem.ActiveSearch.IsValid() || Subsystem.ActiveSearch->SearchState != EOnlineAsyncTaskState::InProgress)
		{
			return false;
		}

		// Release the service's slot first - NULL refuses every later search in the process while it holds one.
		const IOnlineSessionPtr Sessions = Subsystem.GetSessionInterface();
		if (Sessions.IsValid())
		{
			Sessions->CancelFindSessions();
		}

		Subsystem.ActiveSearch->SearchResults = Results;
		Subsystem.ActiveSearch->SearchState = EOnlineAsyncTaskState::Done;
		Subsystem.HandleFindSessionsComplete(true);
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
