// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionRequest.h"

/**
 * FEasySessionPartyRestoreRequest brings back the party this player was in before its last game session.
 * The subsystem creates it for RestoreParty.
 * The host of that party creates it again once, and a member searches for the host's party and joins it.
 *
 * The create, the searches and the join are its sub-requests, so no other request runs between two of them.
 * The party of the last match is kept until a party of this player ends without entering a game session.
 * So a member whose host is still in the match can call RestoreParty again later.
 */
class FEasySessionPartyRestoreRequest final : public FEasySessionRequest
{
public:

	FEasySessionPartyRestoreRequest(int32 InMaxSearchPasses, FEasySessionCompleteDelegate InOnComplete);

	/** Removes the ticker that waits between two search passes. */
	virtual ~FEasySessionPartyRestoreRequest() override;

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	virtual void HandleCancel() override;
	virtual FString GetProgressText() const override;
	//~ End FEasySessionRequest interface

private:

	/** The host's create completed. */
	void HandleCreateComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Search for the host's party once, as a sub-request. */
	void StartSearchPass();

	/** A search completed. Joins the host's party, or waits for the next pass. */
	void HandleSearchComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results);

	/** The join of the host's party completed. */
	void HandleJoinComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The pause between two search passes is over. */
	bool HandlePassDelayElapsed(float DeltaTime);

	/** How many times a member searches for the host's party. */
	int32 MaxSearchPasses = 0;

	/** Search passes completed so far. */
	int32 PassesCompleted = 0;

	/** The requester's delegate. */
	FEasySessionCompleteDelegate OnComplete;

	/** Ticker handle for the pause between two search passes. */
	FTSTicker::FDelegateHandle PassDelayTickerHandle;

	/** Is the join of the host's party running, which a cancel cannot stop. */
	bool bJoining = false;
};
