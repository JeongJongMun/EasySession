// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"
#include "Interfaces/OnlineSessionInterface.h"

class UEasySessionSubsystem;

/**
 * Accepted invites, the friends list and the platform overlays.
 *
 * These use the identity, friends and external UI interfaces rather than the session interface, and no part of the session lifecycle depends on them.
 * A game with no social features never calls into this object at all.
 *
 * There is deliberately no list of received invites.
 * Steam does not report an invite to the game before the player acts on it.
 * The overlay handles that itself, and the game is only told once the player has clicked Join Game.
 * Only EOS reports pending invites, and EOS is not a supported subsystem.
 *
 * Owned by the subsystem and destroyed with it, in Deinitialize.
 * Delegates with handles are bound raw, because Shutdown unbinds them before this object dies.
 * One-shot completion delegates cannot be unbound, so they guard on the subsystem and must not capture this object - it dies first.
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
	 * Open the platform profile overlay for a unique id.
	 * Callers pass the NativeId out of whichever struct they hold, because the overlay only ever needs the id.
	 * Blueprint cannot hold a raw id, which is why the subsystem exposes one typed entry point per struct instead.
	 */
	bool ShowProfileUI(const FUniqueNetIdPtr& TargetId) const;

	/** Read the platform friends list. */
	void ReadFriends(FEasyFriendsCompleteDelegate OnComplete);

	/**
	 * Read the friends list and find the session each friend playing this game is in.
	 * Every query is one request on the owner's queue, one friend at a time - this object only aggregates.
	 */
	void FindFriendSessions(FEasyFriendSessionsCompleteDelegate OnComplete);

private:

	/** Fires when the player accepts an invite from the platform overlay. */
	void HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);

	/** Leave the current session if needed, then join the invited one. */
	void JoinInvitedSession(const FEasySessionSearchResult& Session);

	/**
	 * Second half of JoinInvitedSession, run once leaving the previous session has completed.
	 * A player who did leave is sent to the menu when the join fails, because the session they left is destroyed.
	 *
	 * @param LeaveResult Result of destroying the session this player was in. Anything but Success cancels the join.
	 */
	void JoinInvitedSessionAfterLeaving(EEasySessionResult LeaveResult, const FEasySessionSearchResult& Session);

	/** Ask the queue for the next pending friend's session, or finish the sweep when none are left. */
	void QueryNextFriendSession();

	/** The queue's answer for the friend currently being asked. */
	void HandleFriendQueryComplete(EEasySessionResult Result, const TArray<FEasySessionSearchResult>& Results);

	/** Finish the sweep and report everything it collected. */
	void FinishFriendSessions(EEasySessionResult Result, const FString& ErrorMessage);

	/** The world this subsystem runs in, or null before one exists. */
	UWorld* GetWorld() const;

	UEasySessionSubsystem& Owner;

	/** Handle for the accepted-invite delegate. Valid once BindInviteDelegates has run. */
	FDelegateHandle InviteAcceptedHandle;

	/** True while a friend session sweep runs. */
	bool bFindingFriendSessions = false;

	/** Fired once when the running sweep finishes. */
	FEasyFriendSessionsCompleteDelegate FriendSessionsDelegate;

	/** One entry per friend. Entries of friends playing this game gain their session as the queue answers. */
	TArray<FEasyFriendSession> FriendSessionEntries;

	/** Indices into FriendSessionEntries still waiting for their session query. */
	TArray<int32> PendingFriendQueries;

	/** Index of the entry whose query is on the queue, INDEX_NONE between queries. */
	int32 CurrentFriendQuery = INDEX_NONE;
};
