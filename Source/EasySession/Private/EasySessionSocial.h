// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "Interfaces/OnlineSessionInterface.h"

class UEasySessionSubsystem;

/**
 * Accepted invites, the friends list and the platform overlays.
 *
 * These talk to the identity, friends and external UI interfaces rather than the
 * session interface, and none of the session lifecycle depends on them - a game
 * with no social features never wakes any of this up.
 *
 * There is deliberately no "invite received" list. Steam never tells the game about
 * an invite before the player acts on it: the overlay handles that itself and the
 * game only hears about it once the player has clicked Join Game. Only EOS reports
 * pending invites, and EOS is not a supported subsystem.
 *
 * Owned by the subsystem and destroyed with it. Delegates are bound raw because
 * this object cannot outlive the owner that unbinds them in Shutdown.
 */
class FEasySessionSocial
{
public:

	explicit FEasySessionSocial(UEasySessionSubsystem& InOwner)
		: Owner(InOwner)
	{
	}

	~FEasySessionSocial();

	/** Listen for accepted platform invites. Safe to call again once the session interface exists. */
	void BindInviteDelegates();

	/** Stop listening. Called when the subsystem shuts down. */
	void Shutdown();

	/** Invite a friend to the current session. */
	bool SendInviteToFriend(const FEasySessionFriend& Friend);

	/** Open the platform invite overlay for the current session. */
	bool ShowInviteUI() const;

	/**
	 * Open the platform profile overlay for a unique id. Callers pass the NativeId of
	 * whichever struct they hold - the overlay only ever needed the id, and Blueprint
	 * cannot carry one, which is why the subsystem keeps a typed entry point per struct.
	 */
	bool ShowProfileUI(const FUniqueNetIdPtr& TargetId) const;

	/** Read the platform friends list. */
	void ReadFriends(FEasyFriendsCompleteDelegate OnComplete);

private:

	/** Fires when the player accepts an invite from the platform overlay. */
	void HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);

	/** Leave the current session if needed, then join the invited one. */
	void JoinInvitedSession(const FEasySessionSearchResult& Session);

	/** The world this subsystem runs in, or null before one exists. */
	UWorld* GetWorld() const;

	UEasySessionSubsystem& Owner;

	FDelegateHandle InviteAcceptedHandle;
};
