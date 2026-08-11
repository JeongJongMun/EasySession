// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionServerGate.generated.h"

class AController;
class AGameModeBase;
class APlayerController;
class UEasySessionSubsystem;
struct FUniqueNetIdRepl;

/** Answer to "may this player join?". Decided by FEasySessionServerGate. */
UENUM()
enum class EEasyJoinApprovalResult : uint8
{
	Approved,

	/** The supplied session password did not match. */
	WrongPassword,

	/** Refused for another reason. The reason text says which. */
	Refused,

	/** The asking side could not reach the host. A host never sends this - the beacon client fills it in itself. */
	Unreachable
};

/**
 * The host's door policy: who may join, and who counts as being in the session.
 *
 * Runs on the server only - game modes do not exist on clients. ApproveJoin makes
 * the join decision; the approval beacon asks it before a client travels, and
 * PreLogin asks it again when one arrives. PostLogin and Logout keep the session's
 * player list matching who is actually connected.
 *
 * This object owns the session credentials because it is what enforces them: the
 * password is only ever compared here. The subsystem hands them over when a session
 * is created or updated, and takes them back when it is destroyed.
 *
 * Owned by the subsystem and destroyed with it. Delegates are bound raw because this
 * object cannot outlive the owner that unbinds them in Shutdown.
 */
class FEasySessionServerGate
{
public:

	explicit FEasySessionServerGate(UEasySessionSubsystem& InOwner)
		: Owner(InOwner)
	{
	}

	~FEasySessionServerGate();

	/** Start watching the engine's login events. */
	void Initialize();

	/** Stop watching and forget the credentials. */
	void Shutdown();

	/** Remember what the session this host just created expects from joining players. */
	void SetSessionCredentials(const FString& InPassword, bool bInFriendsBypassPassword);

	/** Forget them once the session is gone. */
	void ClearSessionCredentials();

	/** The password joining players must supply. Empty when the session is open. */
	const FString& GetSessionPassword() const { return SessionPassword; }

	/** Whether friends of the host may join a password session without it. */
	bool GetFriendsBypassPassword() const { return bFriendsBypassPassword; }

	/**
	 * Decide whether a player may join: checks the join-in-progress policy, then the
	 * password, letting friends of the host through without one. Never returns
	 * Unreachable. When refusing, OutReason carries the message shown to the player.
	 */
	EEasyJoinApprovalResult ApproveJoin(const FUniqueNetIdRepl& PlayerId, const FString& SuppliedPassword, FString& OutReason) const;

private:

	/** Engine login event handlers. Server only. */
	void HandlePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage);
	void HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	void HandleLogout(AGameModeBase* GameMode, AController* Exiting);

	/** Whether the event belongs to the world this subsystem runs in. */
	bool IsOwnWorld(const AGameModeBase* GameMode) const;

	UEasySessionSubsystem& Owner;

	/** Password joining players must supply, or empty for an open session. */
	FString SessionPassword;

	/** Whether friends of the host may join a password session without it. */
	bool bFriendsBypassPassword = false;

	FDelegateHandle PreLoginHandle;
	FDelegateHandle PostLoginHandle;
	FDelegateHandle LogoutHandle;
};
