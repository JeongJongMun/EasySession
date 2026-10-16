// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

class FOnlineSessionSettings;

/**
 * FEasySessionCreateRequest creates a session: a game session the host travels to, or a party session with the party beacon on it.
 * Both kinds run the same steps: check what may be created, build the settings, and let the online subsystem create the session.
 * Then the host side or the party beacon starts.
 * Only the checks and the hosting differ by kind, because a party keeps its settings in HostParams and one settings builder serves both.
 *
 * The subsystem creates it for CreateSession and CreateParty.
 * Matchmaking runs it as a sub-request to host a session when no session could be joined.
 * FEasySessionParty runs it to restore the party after a match.
 */
class FEasySessionCreateRequest final : public FEasySessionRequest
{
public:

	/**
	 * Create a game session with InHostParams and travel the host to its Initial Map Name.
	 * The host's reservation holds the party members the host brings.
	 */
	FEasySessionCreateRequest(const FEasySessionHostParams& InHostParams, FEasySessionCompleteDelegate InOnComplete);

	/** Create a party session that advertises InSettings and start the party beacon, with the local player as the leader. */
	FEasySessionCreateRequest(const FEasySessionSettings& InSettings, FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** The public constructors delegate here, so every request names its kind of session. */
	FEasySessionCreateRequest(FName InSessionName, FEasySessionCompleteDelegate InOnComplete);

	/** Whether a session may be created. Completes with the refusal and returns false when it may not. */
	bool CheckCanCreate();

	/** For a game session: refuse host params without an Initial Map Name, and Max Players that cannot fit the host and the party it brings. */
	bool CheckCanCreateGameSession();

	/** For a party: refuse settings a party cannot advertise, a player in a game session, and one with no logged in player to lead. */
	bool CheckCanCreatePartySession();

	/**
	 * Build the settings from HostParams, plus the keys only a new session sets.
	 * A game session also advertises its match state and its reservation beacon.
	 */
	FOnlineSessionSettings MakeSessionSettings() const;

	/** The online subsystem finished creating a session. Sessions with another name are ignored. */
	void HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful);

	/** For a game session: give the host side its params, bring the party, and travel to InitialMapName. */
	void HostGameSession();

	/** For a party: start the party beacon, or destroy the session again when it cannot start. */
	void HostPartySession();

	/**
	 * What the session advertises, and for a game session the map to open the listen server on.
	 * A party leaves the listen server fields at their defaults.
	 */
	FEasySessionHostParams HostParams;

	/** The party members the host brings, whom the host's reservation holds too. Read for a game session only. */
	TArray<FUniqueNetIdRepl> GroupMembers;

	/**
	 * The local player the session is created for: the host, whom the party members search for, or the party leader.
	 * Invalid when no player is logged in.
	 */
	FUniqueNetIdRepl OwnerId;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** Handle for the online subsystem's create completion, bound while the request runs. */
	FDelegateHandle CreateCompleteHandle;
};
