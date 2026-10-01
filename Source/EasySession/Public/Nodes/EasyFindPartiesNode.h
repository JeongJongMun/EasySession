// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyFindPartiesNode.generated.h"

/**
 * Async node that searches for parties.
 */
UCLASS()
class EASYSESSION_API UEasyFindPartiesNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called with the parties found when the search completed successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionFindEvent OnSuccess;

	/** Called when the search failed. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionFindEvent OnFailure;

	/**
	 * Search for parties.
	 * Public parties are listed, and a Join Code finds the party that advertises it.
	 * A result's Session Display Name is the leader's name.
	 *
	 * @param SearchParams Parameters describing what to search for. The game session filters do not apply to parties.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Party", DisplayName = "Find Easy Parties", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "SearchParams"))
	static UEasyFindPartiesNode* FindEasyParties(UObject* WorldContextObject, const FEasySessionSearchParams& SearchParams);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the search. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results);

	/** Parameters to search with. */
	FEasySessionSearchParams SearchParams;
};
