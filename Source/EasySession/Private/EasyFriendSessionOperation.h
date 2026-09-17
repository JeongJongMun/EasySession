// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionOperation.h"
#include "EasySessionTypes.h"

class UEasySessionSubsystem;

/**
 * The friend session search as a queue operation.
 *
 * It reads the friends list, then asks the queue for the session of each friend playing this game, one friend at a time.
 * The next lookup is enqueued only when the previous one completed.
 * A Create or Join asked for meanwhile therefore runs between two lookups instead of after the whole search.
 * A lookup that fails only means no session for that friend; the search goes on.
 *
 * It does not count as busy: it only reads, and nothing about the player's own session changes while it runs.
 */
class FEasyFriendSessionOperation final : public IEasySessionOperation
{
public:

	explicit FEasyFriendSessionOperation(UEasySessionSubsystem& InOwner);

	/**
	 * Read the friends list and start the search.
	 * Call once, after the queue registered this operation: a read that fails inside the call ends the operation before it returns.
	 *
	 * @param InOnComplete Called once with every friend, in display order.
	 */
	void Start(FEasyFriendSessionsCompleteDelegate InOnComplete);

	//~ Begin IEasySessionOperation interface
	virtual EEasySessionOperationType GetType() const override { return EEasySessionOperationType::FriendSearch; }
	virtual bool CountsAsBusy() const override { return false; }
	virtual void Cancel() override;
	virtual FString DescribeProgress() const override;
	//~ End IEasySessionOperation interface

private:

	/** The friends list was read. Every friend gets an entry, and the ones playing this game get a lookup. */
	void HandleFriendsRead(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionFriend>& Friends);

	/** Ask the queue for the next pending friend's session, or finish when none are left. */
	void QueryNext();

	/** The lookup completed for the friend currently asked about. */
	void HandleQueryComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results);

	/** Report everything collected so far, once. */
	void Finish(EEasySessionResult Result, const FString& ErrorMessage);

	UEasySessionSubsystem& Owner;

	/** Fired once when the search finishes. */
	FEasyFriendSessionsCompleteDelegate OnComplete;

	/** One entry per friend. Entries of friends playing this game gain their session as the lookups complete. */
	TArray<FEasyFriendSession> Entries;

	/** Indices into Entries still waiting for their lookup. */
	TArray<int32> Pending;

	/** Index of the entry whose lookup is on the queue, INDEX_NONE between lookups. */
	int32 Current = INDEX_NONE;

	/** How many lookups the search asks for in total, for the status line. */
	int32 LookupCount = 0;

	/** Whether Finish ran. A late completion after a cancel is ignored. */
	bool bFinished = false;
};
