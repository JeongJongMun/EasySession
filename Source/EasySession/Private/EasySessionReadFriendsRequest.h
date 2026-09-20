// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionRequest.h"

/**
 * FEasySessionReadFriendsRequest reads the local player's friends list and orders it for display.
 *
 * The subsystem creates it for Read Easy Friends.
 * The friend session search runs it as its first sub-request.
 *
 * Keep in mind that NULL has no friends list, so callers check HasFriendsList first and refuse inside the call.
 */
class FEasySessionReadFriendsRequest final : public FEasySessionRequest
{
public:

	explicit FEasySessionReadFriendsRequest(FEasyFriendsCompleteDelegate InOnComplete);

	/** @return Whether the online subsystem of this world has a friends list. NULL has none. */
	static bool HasFriendsList(const UWorld* World);

	/** @return Whether A comes before B in display order: playing this game first, then online, then offline, each group by name. */
	static bool FriendComesFirst(const FEasySessionFriend& A, const FEasySessionFriend& B);

	/** Order friends for display, see FriendComesFirst. */
	static void SortFriends(TArray<FEasySessionFriend>& Friends);

protected:

	//~ Begin FEasySessionRequest interface
	virtual void Execute() override;
	virtual void Notify(EEasySessionResult Result, const FString& ErrorMessage) override;
	//~ End FEasySessionRequest interface

private:

	/** The online subsystem finished reading the friends list. */
	void HandleReadFriendsListComplete(int32 LocalUserNum, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr);

	/** The requester's delegate. */
	FEasyFriendsCompleteDelegate OnComplete;

	/** The friends in display order. Only filled after a successful read. */
	TArray<FEasySessionFriend> Friends;
};
