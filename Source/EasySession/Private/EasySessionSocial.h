// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionTypes.h"

class UEasySessionSubsystem;

/**
 * FEasySessionSocial is responsible for the platform's social features that need no request: accepted invites, sent invites and the overlays.
 * Reading the friends list and the friend session search are requests, see FEasySessionReadFriendsRequest and FEasySessionFriendSessionsRequest.
 * An accepted invite is joined with JoinSession or JoinParty, the same calls a game uses.
 *
 * These use the identity and external UI interfaces, and no part of the session lifecycle depends on them.
 * A game with no social features never calls into this object at all.
 *
 * There is deliberately no list of received invites.
 * Steam does not report an invite to the game before the player acts on it.
 * The overlay handles that itself, and the game is only told once the player has clicked Join Game.
 * Only EOS reports pending invites, and EOS is not a supported subsystem.
 *
 * Owned by the subsystem and destroyed with it, in Deinitialize.
 * The accepted-invite delegate is bound raw, because Shutdown unbinds it before this object is destroyed.
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

	/**
	 * Invite a friend to the game session or to the party.
	 *
	 * @param SessionName NAME_GameSession or NAME_PartySession.
	 * @return Success, or why the invite could not be sent.
	 */
	EEasySessionResult SendInviteToFriend(const FEasySessionFriend& Friend, FName SessionName);

	/**
	 * Open the platform invite overlay for the game session or for the party.
	 *
	 * @param SessionName NAME_GameSession or NAME_PartySession.
	 * @return Success, or why the overlay could not be opened.
	 */
	EEasySessionResult ShowInviteUI(FName SessionName) const;

	/**
	 * Open the platform profile overlay for a unique id.
	 * Callers pass the NativeId out of whichever struct they hold, because the overlay only ever needs the id.
	 * Blueprint cannot hold a raw id, which is why the subsystem exposes one typed entry point per struct instead.
	 */
	EEasySessionResult ShowProfileUI(const FUniqueNetIdPtr& TargetId) const;

private:

	/**
	 * Fires when the player accepts an invite from the platform overlay.
	 * Joins the session or the party when bAutoJoinAcceptedInvites is on.
	 * A player in a game session joins a session only when bAcceptInvitesWhileInSession is on, and never joins a party.
	 */
	void HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);

	/** Join the party of an accepted invite, after a LeaveParty request for the party this player is in. */
	void JoinInvitedParty(const FEasySessionSearchResult& Party);

	/** The world this subsystem runs in, or null before one exists. */
	UWorld* GetWorld() const;

	UEasySessionSubsystem& Owner;

	/** Handle for the accepted-invite delegate. Valid once BindInviteDelegates has run. */
	FDelegateHandle InviteAcceptedHandle;
};
