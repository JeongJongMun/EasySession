// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionServerGate.h"
#include "EasySessionSubsystem.h"
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
};

#endif // WITH_DEV_AUTOMATION_TESTS
