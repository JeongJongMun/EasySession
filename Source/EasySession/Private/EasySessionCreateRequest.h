// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

class FOnlineSessionSettings;

/**
 * Creates the session and travels the host to Initial Map Name.
 * On success the host side of the session gets the params before the travel is requested.
 */
class FEasySessionCreateRequest final : public FEasySessionRequest
{
public:

	FEasySessionCreateRequest(const FEasySessionHostParams& InHostParams, FEasySessionCompleteDelegate InOnComplete);

	/** Build the settings a new session is created and advertised with. */
	static FOnlineSessionSettings MakeSessionSettings(const FEasySessionHostParams& Params, bool bForceLAN);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup(bool bAbandoned) override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** The online subsystem finished creating a session. Sessions with another name are ignored. */
	void HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful);

	/** The session to advertise, and the map to open the listen server on. */
	FEasySessionHostParams HostParams;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** Handle for the online subsystem's create completion, bound while the request runs. */
	FDelegateHandle CreateCompleteHandle;
};
