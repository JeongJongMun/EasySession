// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EasySessionServerGate.h"
#include "EasySessionSubsystem.h"
#include "EasySessionTypes.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"

/**
 * The subsystem's private state, reached on behalf of the tests.
 *
 * Tests need to see things no game should: which of two places a value came from,
 * or a fact the join path would normally decide. Keeping those reads here instead
 * of on the subsystem means the plugin a user installs carries no test API, in any
 * build configuration.
 */
class FEasySessionTestAccess
{
public:

	/**
	 * Pretend this process did or did not create the active session. Joining is what
	 * normally settles it, and a headless test has no second process to join.
	 */
	static void SetCreatedActiveSession(UEasySessionSubsystem& Subsystem, bool bCreated)
	{
		Subsystem.bCreatedActiveSession = bCreated;
	}

	/**
	 * The host state the replicated carrier last delivered. Get Session State only
	 * surfaces this on an actual client, which a headless test world is not.
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
	 * The password-protected flag as it is advertised to searching players. Paired
	 * with the enforced password above: a test proves the two agree by reading both.
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
};

#endif // WITH_DEV_AUTOMATION_TESTS
