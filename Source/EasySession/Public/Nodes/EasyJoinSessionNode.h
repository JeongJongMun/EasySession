// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyJoinSessionNode.generated.h"

/**
 * Async node that joins a session found by a search.
 */
UCLASS()
class EASYSESSION_API UEasyJoinSessionNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was joined successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the session could not be joined. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Join the given session and optionally travel to the host.
	 *
	 * @param SearchResult A search result returned by Find Easy Sessions.
	 * @param bTravelOnSuccess Whether to travel to the host once joined.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Join Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "SearchResult"))
	static UEasyJoinSessionNode* JoinEasySession(UObject* WorldContextObject, const FEasySessionSearchResult& SearchResult, bool bTravelOnSuccess = true);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the join operation. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The session to join. */
	FEasySessionSearchResult SearchResult;

	/** Whether to travel to the host once joined. */
	bool bTravelOnSuccess = true;
};
