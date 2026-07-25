// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "Interfaces/OnlineSessionInterface.h"

class UEasySessionSubsystem;

/**
 * Invites, the friends list and the platform overlays.
 *
 * These talk to the identity, friends and external UI interfaces rather than the
 * session interface, and none of the session lifecycle depends on them - a game
 * with no social features never wakes any of this up. Keeping them here means the
 * subsystem does not carry the invite list, the overlay plumbing and their delegate
 * handles alongside the session flow.
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

	/** Listen for platform invites. Safe to call again once the session interface exists. */
	void BindInviteDelegates();

	/** Stop listening. Called when the subsystem shuts down. */
	void Shutdown();

	/** Invites received so far, oldest first. */
	const TArray<FEasySessionInvite>& GetPendingInvites() const { return PendingInvites; }

	/** Forget every pending invite, e.g. after showing them once. */
	void ClearPendingInvites() { PendingInvites.Empty(); }

	/** Join the session an invite points to and drop it from the pending list. */
	void AcceptInvite(const FEasySessionInvite& Invite);

	/** Invite a friend to the current session. */
	bool SendInviteToFriend(const FEasySessionFriend& Friend);

	/** Open the platform invite overlay for the current session. */
	bool ShowInviteUI() const;

	/** Open the platform profile overlay for a friend or for a player in the session. */
	bool ShowProfileUI(const FEasySessionFriend& Friend) const;
	bool ShowProfileUIForPlayer(const FEasySessionPlayerInfo& Player) const;

	/** Read the platform friends list. */
	void ReadFriends(FEasyFriendsCompleteDelegate OnComplete);

private:

	/** Online subsystem delegate handlers. */
	void HandleSessionInviteReceived(const FUniqueNetId& UserId, const FUniqueNetId& FromId, const FString& AppId, const FOnlineSessionSearchResult& InviteResult);
	void HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);

	/** Leave the current session if needed, then join the invited one. */
	void JoinInvitedSession(const FEasySessionSearchResult& Session);

	bool ShowProfileUIInternal(const FUniqueNetIdPtr& TargetId) const;

	/** The world this subsystem runs in, or null before one exists. */
	UWorld* GetWorld() const;

	UEasySessionSubsystem& Owner;

	/** Invites received but not yet accepted or cleared. */
	TArray<FEasySessionInvite> PendingInvites;

	/** Whether a friends list read is in flight - the interface allows only one. */
	bool bReadingFriends = false;

	FDelegateHandle InviteReceivedHandle;
	FDelegateHandle InviteAcceptedHandle;
};
