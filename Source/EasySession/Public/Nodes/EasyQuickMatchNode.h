// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyQuickMatchNode.generated.h"

class UEasyMatchmakingPolicy;

/**
 * Async node that runs QuickMatch matchmaking.
 */
UCLASS()
class EASYSESSION_API UEasyQuickMatchNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/**
	 * Called when matchmaking finished in a session.
	 * Use Is Easy Session Host to check whether we joined a session or hosted our own.
	 */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when matchmaking failed or was canceled. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Start QuickMatch matchmaking: search for sessions, join the best one, and optionally host a new session when nothing is found.
	 * Cancel a running QuickMatch with Cancel Easy Matchmaking.
	 *
	 * @param QuickMatchParams Parameters describing the search and the fallback host session.
	 * @param PolicyClass Optional custom matchmaking policy class. Uses the default policy when empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Quick Match Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "QuickMatchParams"))
	static UEasyQuickMatchNode* QuickMatchEasySession(UObject* WorldContextObject, const FEasyQuickMatchParams& QuickMatchParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass = nullptr);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes matchmaking. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Parameters for the matchmaking run. */
	FEasyQuickMatchParams QuickMatchParams;

	/** Optional custom policy class. */
	UPROPERTY()
	TSubclassOf<UEasyMatchmakingPolicy> PolicyClass;
};
