// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

class FOnlineSessionSettings;

/**
 * FEasySessionCreatePartyRequest creates the party session and starts the party beacon, with the local player as the leader.
 * A party needs no map, so nothing travels.
 * A party beacon that cannot start destroys the party session again, so a party never exists without its member list.
 *
 * The subsystem creates it for Create Easy Party.
 */
class FEasySessionCreatePartyRequest final : public FEasySessionRequest
{
public:

	FEasySessionCreatePartyRequest(const FEasyPartyParams& InPartyParams, FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/**
	 * Build the settings the party session is created and advertised with.
	 *
	 * @param LeaderId The logged in player who leads the party.
	 * @param LeaderName The leader's name, which a search lists the party under.
	 */
	static FOnlineSessionSettings MakePartySettings(const FEasyPartyParams& Params, bool bForceLAN, const FUniqueNetIdRepl& LeaderId, const FString& LeaderName);

	/** The online subsystem finished creating a session. Sessions with another name are ignored. */
	void HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful);

	/** The party to create. */
	FEasyPartyParams PartyParams;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** Handle for the online subsystem's create completion, bound while the request runs. */
	FDelegateHandle CreateCompleteHandle;
};
