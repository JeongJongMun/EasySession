// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyFindFriendSessionsNode.generated.h"

/**
 * Async node that reads the friends list and finds the session each friend is in.
 */
UCLASS()
class EASYSESSION_API UEasyFindFriendSessionsNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called with one entry per friend when the search completed. Entries with Has Session carry a session joinable with Join Easy Session. */
	UPROPERTY(BlueprintAssignable)
	FEasyFriendSessionsEvent OnSuccess;

	/** Called when the search failed or is not supported (e.g. NULL/LAN). */
	UPROPERTY(BlueprintAssignable)
	FEasyFriendSessionsEvent OnFailure;

	/**
	 * Read the friends list and find the session each friend playing this game is in.
	 * Every friend is listed; the ones in a joinable session carry it, ready for Join Easy Session.
	 * Not supported on the NULL (LAN) subsystem.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Find Easy Friend Sessions", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEasyFindFriendSessionsNode* FindEasyFriendSessions(UObject* WorldContextObject);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the search. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasyFriendSession>& FriendSessions);
};
