// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

/**
 * FEasySessionLeavePartyRequest closes the party beacon and destroys the party session.
 * A leader who leaves ends the party for every member, because the member list lives on the leader's beacon.
 *
 * The subsystem creates it for Leave Easy Party.
 */
class FEasySessionLeavePartyRequest final : public FEasySessionRequest
{
public:

	explicit FEasySessionLeavePartyRequest(FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** The party session was destroyed, or could not be. */
	void HandleDestroyComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;
};
