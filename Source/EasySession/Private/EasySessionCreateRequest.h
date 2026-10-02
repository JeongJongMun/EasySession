// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

class FOnlineSessionSettings;

/**
 * FEasySessionCreateRequest creates the session and travels the host to Initial Map Name.
 * On success the host side of the session gets the params before the travel is requested.
 *
 * The subsystem creates it for Create Easy Session.
 * Matchmaking runs it as a sub-request to host a session when no session could be joined.
 */
class FEasySessionCreateRequest final : public FEasySessionRequest
{
public:

	FEasySessionCreateRequest(const FEasySessionHostParams& InHostParams, FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/**
	 * Build the settings a new session is created and advertised with: the host params, plus what only a new session sets.
	 *
	 * @param OwnerId The player the online subsystem creates the session for. Invalid when no player is logged in.
	 */
	static FOnlineSessionSettings MakeSessionSettings(const FEasySessionHostParams& Params, bool bForceLAN, const FUniqueNetIdRepl& OwnerId);

	/** The online subsystem finished creating a session. Sessions with another name are ignored. */
	void HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful);

	/** The session to advertise, and the map to open the listen server on. */
	FEasySessionHostParams HostParams;

	/** The party members the host brings. The host's reservation holds them too. */
	TArray<FUniqueNetIdRepl> GroupMembers;

	/** The local player the session is created for, whom the party members search for. */
	FUniqueNetIdRepl OwnerId;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** Handle for the online subsystem's create completion, bound while the request runs. */
	FDelegateHandle CreateCompleteHandle;
};
