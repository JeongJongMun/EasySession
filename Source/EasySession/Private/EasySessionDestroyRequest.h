// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

/**
 * FEasySessionDestroyRequest destroys this game's session.
 * On success the host side of the session and the state a client received through replication are cleared.
 *
 * The subsystem creates it for Destroy Easy Session, Leave Easy Session and a lost connection.
 * Create, Join and matchmaking run it as a sub-request to destroy a session they created or joined.
 */
class FEasySessionDestroyRequest final : public FEasySessionRequest
{
public:

	explicit FEasySessionDestroyRequest(FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** The online subsystem finished destroying a session. Sessions with another name are ignored. */
	void HandleDestroySessionComplete(FName InSessionName, bool bWasSuccessful);

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** Handle for the online subsystem's destroy completion, bound while the request runs. */
	FDelegateHandle DestroyCompleteHandle;
};
