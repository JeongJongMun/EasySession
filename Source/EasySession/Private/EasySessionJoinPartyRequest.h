// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EasySessionRequest.h"

/**
 * FEasySessionJoinPartyRequest joins a party a search returned, then logs the local player in on the leader's party beacon.
 * It completes with Success once the member list holds the local player, so Get Easy Party Members already lists them.
 * A party needs no map, so nothing travels.
 *
 * The leader decides the join at the login, and a refusal arrives with its reason.
 * A refused, unreachable or timed out join destroys the party session again, so the player is in no party afterwards.
 *
 * The subsystem creates it for JoinParty, and FEasySessionParty creates it to restore the party after a match.
 */
class FEasySessionJoinPartyRequest final : public FEasySessionRequest
{
public:

	FEasySessionJoinPartyRequest(const FEasySessionSearchResult& InTarget, FEasySessionCompleteDelegate InOnComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** The online subsystem finished joining a session. Sessions with another name are ignored. */
	void HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type JoinResult);

	/** The leader's party beacon admitted the local player, or did not. */
	void HandleConnectComplete(bool bSuccess, const FText& Reason);

	/** The leader did not complete the login in time. */
	bool HandleTimeout(float DeltaTime);

	/** Close the party beacon, destroy the party session again, then complete with this result. */
	void LeaveAndComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The party to join, as returned by a search. */
	FEasySessionSearchResult Target;

	/** Called when the request completes. */
	FEasySessionCompleteDelegate OnComplete;

	/** Handle for the online subsystem's join completion, bound while the request runs. */
	FDelegateHandle JoinCompleteHandle;

	/** Ticker that ends the join when the leader does not complete the login in time. */
	FTSTicker::FDelegateHandle TimeoutHandle;
};
