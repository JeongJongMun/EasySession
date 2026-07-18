// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyFindSessionsNode.generated.h"

/**
 * Async node that searches for sessions.
 */
UCLASS()
class EASYSESSION_API UEasyFindSessionsNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called with the filtered results when the search completed successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionFindEvent OnSuccess;

	/** Called when the search failed. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionFindEvent OnFailure;

	/**
	 * Search for sessions matching the given filters.
	 * Results can also be read back later via Get Last Search Results.
	 *
	 * @param SearchParams Parameters describing what to search for.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Find Easy Sessions", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "SearchParams"))
	static UEasyFindSessionsNode* FindEasySessions(UObject* WorldContextObject, const FEasySessionSearchParams& SearchParams);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the search. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results);

	/** Parameters to search with. */
	FEasySessionSearchParams SearchParams;
};
