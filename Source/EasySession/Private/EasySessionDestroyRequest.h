// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

/**
 * Destroys this game's named session.
 * On success the host side of the session and the state a client received through replication are cleared.
 */
class FEasySessionDestroyRequest final : public FEasySessionRequest
{
public:

	explicit FEasySessionDestroyRequest(FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup(bool bAbandoned) override;
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
