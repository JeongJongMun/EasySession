// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

/**
 * FEasySessionLeaveRequest leaves a session: the game session, after which the player travels to the Game Default Map, or the party.
 * Both kinds run the same steps: close the beacon or tell the clients, destroy the session, then end the party or travel to the menu.
 * A host that leaves its game session sends every client the reason first, and a party closes its beacon first.
 *
 * The subsystem creates it for LeaveSession, DestroySessionForEveryone, LeaveParty and HandlePartyEnded.
 */
class FEasySessionLeaveRequest final : public FEasySessionRequest
{
public:

	/**
	 * Leave the game session and travel to the Game Default Map.
	 *
	 * @param InReason The reason a host sends every client before the session is destroyed. A client sends nothing.
	 */
	FEasySessionLeaveRequest(const FText& InReason, FEasySessionCompleteDelegate InOnComplete);

	/**
	 * Leave the party. A leader who leaves ends the party for every member, because the member list lives on the leader's beacon.
	 *
	 * @param InReason Why the party ends for the local player, which OnPartyLeft reports.
	 */
	FEasySessionLeaveRequest(EEasyPartyLeaveReason InReason, const FText& InReasonText, FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** The public constructors delegate here, so every request names its kind of session. */
	FEasySessionLeaveRequest(FName InSessionName, FEasySessionCompleteDelegate InOnComplete);

	/** The Destroy sub-request completed: the session was destroyed, or could not be. The player is out of it either way. */
	void HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Why the party ends for the local player. Read for a party only. */
	EEasyPartyLeaveReason PartyReason = EEasyPartyLeaveReason::Left;

	/** The reason a host sends its clients, or the text OnPartyLeft reports with PartyReason. */
	FText ReasonText;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;
};
