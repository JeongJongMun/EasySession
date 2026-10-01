// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

class FOnlineSessionSearch;
class FOnlineSessionSearchResult;

/**
 * FEasySessionFindRequest searches for sessions and filters what the online subsystem returns.
 * Search Mode By Friend asks for the one session a friend is in, which the online subsystem completes through its own delegate.
 *
 * The subsystem creates it for Find Easy Sessions.
 * Matchmaking runs it as a sub-request for each search pass, and the friend session search runs it for each friend.
 *
 * The request owns the native search object while it runs.
 * The online subsystem holds that object too and refuses every later search until it is released, so Cleanup releases it on every path.
 *
 * A LAN search stops when the request is canceled.
 * The online subsystem cannot stop an internet search, so a canceled one keeps running until it ends, see FEasySessionRequest::HandleCancel.
 */
class FEasySessionFindRequest final : public FEasySessionRequest
{
	//~ FEasySessionTestAccess reads and completes the native search for the tests.
	friend class FEasySessionTestAccess;

public:

	FEasySessionFindRequest(const FEasySessionSearchParams& InSearchParams, FEasySessionFindCompleteDelegate InOnFindComplete);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	virtual void HandleCancel() override;
	//~ End FEasySessionRequest interface

private:

	/** Ask for the session a friend is in. It runs without a search object and completes through its own delegate. */
	void FindFriendSession();

	/** The online subsystem finished a friend session query. */
	void HandleFindFriendSessionComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<FOnlineSessionSearchResult>& FriendResults);

	/** Start a search for sessions, completing the request on the failures the online subsystem reports inside the call. */
	void FindSessions();

	/** The online subsystem finished a search for sessions. Every search in the process fires this delegate, so searches of other requesters are ignored. */
	void HandleFindSessionsComplete(bool bWasSuccessful);

	/** Filter what the online subsystem returned and complete with Success. */
	void CompleteWithResults(const TArray<FOnlineSessionSearchResult>& NativeResults);

	/** @return Whether this request looks for parties rather than game sessions. */
	bool IsPartySearch() const;

	/** The filters to search with, including the targeted-query ids. */
	FEasySessionSearchParams SearchParams;

	/** The requester's delegate. */
	FEasySessionFindCompleteDelegate OnFindComplete;

	/** The native search object of a search for sessions. Only valid while that search runs. */
	TSharedPtr<FOnlineSessionSearch> Search;

	/** The filtered results, passed to the requester. */
	TArray<FEasySessionSearchResult> Results;

	/** Handle for the completion of a search for sessions, bound while the request runs. */
	FDelegateHandle FindCompleteHandle;

	/** Handle for the completion of a friend session query, bound while the request runs. */
	FDelegateHandle FindFriendCompleteHandle;
};
