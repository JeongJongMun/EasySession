// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyQuickMatchNode.generated.h"

class UEasyQuickMatchPolicy;

/**
 * Async node that runs QuickMatch quick match.
 */
UCLASS()
class EASYSESSION_API UEasyQuickMatchNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/**
	 * Called when quick match finished in a session.
	 * Use Is Easy Session Host to check whether we joined a session or hosted our own.
	 */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when quick match failed or was canceled. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Start QuickMatch quick match: search for sessions, join the best one, and optionally host a new session when nothing is found.
	 * Cancel a running QuickMatch with Cancel Easy QuickMatch.
	 *
	 * @param QuickMatchParams Parameters describing the search and the fallback host session.
	 * @param PolicyClass Optional custom quick match policy class. Uses the default policy when empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Quick Match Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "QuickMatchParams"))
	static UEasyQuickMatchNode* QuickMatchEasySession(UObject* WorldContextObject, const FEasyQuickMatchParams& QuickMatchParams, TSubclassOf<UEasyQuickMatchPolicy> PolicyClass = nullptr);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes quick match. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Parameters for the quick match run. */
	FEasyQuickMatchParams QuickMatchParams;

	/** Optional custom policy class. */
	UPROPERTY()
	TSubclassOf<UEasyQuickMatchPolicy> PolicyClass;
};
