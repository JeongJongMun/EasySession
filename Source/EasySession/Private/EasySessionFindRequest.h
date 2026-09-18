// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

class FOnlineSessionSearch;
class FOnlineSessionSearchResult;

/**
 * Searches for sessions and filters what the online subsystem returns.
 * Search Mode By Friend asks for the one session a friend is in, which the online subsystem completes through its own delegate.
 *
 * The request owns the native search object while it runs.
 * The online subsystem holds that object too and refuses every later search until it is released, so Cleanup releases it on every path.
 *
 * The requester can cancel the search, see Cancel.
 */
class FEasySessionFindRequest final : public FEasySessionRequest
{
	//~ FEasySessionTestAccess reads and completes the native search for the tests.
	friend class FEasySessionTestAccess;

public:

	FEasySessionFindRequest(const FEasySessionSearchParams& InSearchParams, FEasySessionFindCompleteDelegate InOnFindComplete);

	/** @return The request as a Find request when its type says it is one. Null otherwise. */
	static TSharedPtr<FEasySessionFindRequest> Cast(const TSharedPtr<FEasySessionRequest>& Request);

	/**
	 * Cancel the search for the object that requested it.
	 * A LAN search can be stopped, so the request completes with Canceled inside this call.
	 * The online subsystem cannot stop an internet search.
	 * The request then keeps the active slot, Canceled is delivered to the requester inside this call and the late completion is dropped.
	 *
	 * @return Whether the requester's delegate belongs to this request.
	 */
	bool Cancel(const UObject* Requester);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Cleanup(bool bAbandoned) override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	virtual float GetTimeoutOverrideSeconds() const override { return SearchParams.TimeoutOverrideSeconds; }
	//~ End FEasySessionRequest interface

private:

	/** Ask for the session a friend is in. It runs without a search object and completes through its own delegate. */
	void StartFriendSessionSearch();

	/** Start a discovery search, completing the request on the failures the online subsystem reports inside the call. */
	void StartSessionSearch();

	/** The online subsystem finished a discovery search. Every search in the process fires this delegate, so searches of other requesters are ignored. */
	void HandleFindSessionsComplete(bool bWasSuccessful);

	/** The online subsystem finished a friend query. */
	void HandleFindFriendSessionComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<FOnlineSessionSearchResult>& FriendResults);

	/** Filter what the search returned and complete with Success. */
	void FinishSearch(const TArray<FOnlineSessionSearchResult>& NativeResults);

	/** The filters to search with, including the targeted-query ids. */
	FEasySessionSearchParams SearchParams;

	/** The requester's delegate. Unbound when the requester cancels. */
	FEasySessionFindCompleteDelegate OnFindComplete;

	/** The native search object of a discovery search. Only valid while that search runs. */
	TSharedPtr<FOnlineSessionSearch> Search;

	/** The filtered results, delivered to the requester and stored as the last search results. */
	TArray<FEasySessionSearchResult> Results;

	/** Handle for the discovery search completion, bound while the request runs. */
	FDelegateHandle FindCompleteHandle;

	/** Handle for the friend query completion, bound while the request runs. */
	FDelegateHandle FindFriendCompleteHandle;
};
