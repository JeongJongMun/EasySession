// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

/**
 * FEasySessionLeavePartyRequest closes the party beacon and destroys the party session, then broadcasts On Party Left.
 * A leader who leaves ends the party for every member, because the member list lives on the leader's beacon.
 *
 * The subsystem creates it for Leave Easy Party, with the reason Left.
 * It also creates it when the leader ended this player's membership, with the reason the leader sent or Connection Lost.
 */
class FEasySessionLeavePartyRequest final : public FEasySessionRequest
{
public:

	/** @param InReason Why the local player leaves, which On Party Left reports. */
	FEasySessionLeavePartyRequest(EEasyPartyLeaveReason InReason, const FText& InReasonText, FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** The party session was destroyed, or could not be. The party is over for this player either way. */
	void HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Why the local player leaves. */
	EEasyPartyLeaveReason Reason;

	/** The text On Party Left reports with the reason. */
	FText ReasonText;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;
};
