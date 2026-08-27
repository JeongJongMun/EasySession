// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionServerGate.generated.h"

class AGameModeBase;
class UEasySessionSubsystem;
struct FUniqueNetIdRepl;

/** Answer to "may this player join?". Decided by FEasySessionServerGate. */
UENUM()
enum class EEasyJoinApprovalResult : uint8
{
	/** The player may join. */
	Approved,

	/** The supplied session password did not match. */
	WrongPassword,

	/** The session has no room for another player. */
	SessionFull,

	/** Refused for another reason. The reason text says which. */
	Refused,

	/** The joining client could not reach the host. A host never sends this - the beacon client fills it in itself. */
	Unreachable
};

/**
 * Decides who may join the session.
 *
 * Runs on the server only, because game modes do not exist on clients.
 * ApproveJoin makes the decision: the approval beacon asks it before a client travels, and PreLogin asks it again when that client arrives.
 *
 * This object holds the session credentials because it is the only place the password is ever compared.
 * The subsystem passes them in when a session is created or updated, and clears them when it is destroyed.
 *
 * Owned by the subsystem and destroyed with it.
 * Delegates are bound raw because this object cannot outlive the owner that unbinds them in Shutdown.
 */
class FEasySessionServerGate
{
public:

	explicit FEasySessionServerGate(UEasySessionSubsystem& InOwner)
		: Owner(InOwner)
	{
	}

	~FEasySessionServerGate();

	/** Start watching the engine's PreLogin event. */
	void Initialize();

	/** Stop watching and forget the credentials. */
	void Shutdown();

	/** Remember what the session this host just created expects from joining players. */
	void SetSessionCredentials(const FString& InPassword, bool bInFriendsBypassPassword);

	/** Forget the credentials once the session is gone. */
	void ClearSessionCredentials();

	/** The password joining players must supply. Empty when the session is open. */
	const FString& GetSessionPassword() const { return SessionPassword; }

	/** Whether friends of the host may join a password session without it. */
	bool GetFriendsBypassPassword() const { return bFriendsBypassPassword; }

	/**
	 * Decide whether a player may join.
	 * Checks the join-in-progress policy first, then the room the session has left, then the password, which friends of the host may skip.
	 * Never returns Unreachable, which only the beacon client produces.
	 *
	 * @param OutReason Set to the message shown to the refused player. Untouched when the join is approved.
	 */
	EEasyJoinApprovalResult ApproveJoin(const FUniqueNetIdRepl& PlayerId, const FString& SuppliedPassword, FString& OutReason) const;

private:

	/** Refuse the arriving player when ApproveJoin says no, by writing the reason into ErrorMessage. */
	void HandlePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage);

	/** Whether the event belongs to the world this subsystem runs in. Ignores PIE instances other than our own. */
	bool IsOwnWorld(const AGameModeBase* GameMode) const;

	UEasySessionSubsystem& Owner;

	/** Password joining players must supply, or empty for an open session. */
	FString SessionPassword;

	/** Whether friends of the host may join a password session without it. */
	bool bFriendsBypassPassword = false;

	FDelegateHandle PreLoginHandle;
};
