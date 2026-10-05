// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

/**
 * FEasySessionFriendSessionsRequest is responsible for the friend session search.
 * It reads the friends list, then searches for the session of each friend playing this game, one friend at a time.
 * The subsystem creates it for FindFriendSessions.
 *
 * Reading the friends list and the searches for each friend's session are its sub-requests, so no other request runs between two of them.
 * A search that fails only means no session for that friend, and the friend session search goes on.
 */
class FEasySessionFriendSessionsRequest final : public FEasySessionRequest
{
public:

	explicit FEasySessionFriendSessionsRequest(FEasyFriendSessionsCompleteDelegate InOnComplete);

	/** Order friend sessions for display: friends in a joinable session first, then FEasySessionReadFriendsRequest::FriendComesFirst. */
	static void SortFriendSessions(TArray<FEasyFriendSession>& FriendSessions);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	virtual void HandleCancel() override;
	virtual FString GetProgressText() const override;
	//~ End FEasySessionRequest interface

private:

	/** The friends list was read. Every friend gets a friend session, and the session of each friend playing this game is searched for. */
	void HandleFriendsRead(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionFriend>& Friends);

	/** Search for the next friend's session as a sub-request, or complete when no friend is left. */
	void FindNextFriendSession();

	/** The search for the session of the friend in SearchingFriend completed. */
	void HandleFriendSessionFindComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results);

	/** The requester's delegate, fired once with every friend in display order. */
	FEasyFriendSessionsCompleteDelegate OnComplete;

	/** One per friend. Friends playing this game get their session as the searches complete. */
	TArray<FEasyFriendSession> FriendSessions;

	/** Indices into FriendSessions of the friends whose session was not searched for yet. */
	TArray<int32> FriendsToSearch;

	/** Index into FriendSessions of the friend whose session is searched for right now, INDEX_NONE between two searches. */
	int32 SearchingFriend = INDEX_NONE;

	/** How many friends' sessions the search runs for in total, for the status line. */
	int32 SearchTotal = 0;
};
