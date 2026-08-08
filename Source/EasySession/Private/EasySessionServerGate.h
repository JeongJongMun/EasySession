// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

class AController;
class AGameModeBase;
class APlayerController;
class UEasySessionSubsystem;
struct FUniqueNetIdRepl;

/**
 * The host's door policy: who may connect, and who counts as being in the session.
 *
 * Everything here runs on the server only - game modes do not exist on clients - and
 * reacts to engine events (PreLogin, PostLogin, Logout) rather than to anything the
 * local player asks for. That is a different job from driving the local session, so
 * it lives apart from the subsystem's own flow.
 *
 * This object owns the session credentials because it is what enforces them: the
 * password is only ever compared here, and the subsystem hands it over when a
 * session is created and takes it back when one is destroyed.
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
