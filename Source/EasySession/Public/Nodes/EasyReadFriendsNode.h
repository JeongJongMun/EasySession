// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyReadFriendsNode.generated.h"

/**
 * Async node that reads the local player's friends list.
 */
UCLASS()
class EASYSESSION_API UEasyReadFriendsNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called with the friends when the read completed successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasyFriendsEvent OnSuccess;

	/** Called when the read failed or is not supported (e.g. NULL/LAN). */
	UPROPERTY(BlueprintAssignable)
	FEasyFriendsEvent OnFailure;

	/**
	 * Read the local player's friends list from the online service.
	 * Not supported on the NULL (LAN) subsystem.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Read Easy Friends", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEasyReadFriendsNode* ReadEasyFriends(UObject* WorldContextObject);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the read. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionFriend>& Friends);
};
