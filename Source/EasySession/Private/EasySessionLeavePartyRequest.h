// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

/**
 * FEasySessionLeavePartyRequest closes the party beacon and destroys the party session, then broadcasts OnPartyLeft.
 * A leader who leaves ends the party for every member, because the member list lives on the leader's beacon.
 *
 * The subsystem creates it for LeaveParty with the reason Left.
 * HandlePartyEnded on the subsystem creates it with Kicked, LeaderLeft, ConnectionLost or MovedToGameSession.
 */
class FEasySessionLeavePartyRequest final : public FEasySessionRequest
{
public:

	/** @param InReason Why the party ends for the local player, which OnPartyLeft reports. */
	FEasySessionLeavePartyRequest(EEasyPartyLeaveReason InReason, const FText& InReasonText, FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** The party session was destroyed, or could not be. The party is over for this player either way. */
	void HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Why the party ends for the local player. */
	EEasyPartyLeaveReason Reason;

	/** The text OnPartyLeft reports with the reason. */
	FText ReasonText;

	/** Called when the request completes. */
	FEasySessionCompleteDelegate OnComplete;
};
