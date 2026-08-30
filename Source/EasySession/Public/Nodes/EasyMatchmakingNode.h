// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyMatchmakingNode.generated.h"

class UEasyMatchmakingPolicy;

/**
 * Async node that runs Matchmaking.
 */
UCLASS()
class EASYSESSION_API UEasyMatchmakingNode : public UEasySessionNodeBase
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
	 * Start Matchmaking: search for sessions, join the best one, and optionally host a new session when nothing is found.
	 * Cancel a running Matchmaking with Cancel Easy Matchmaking.
	 *
	 * @param MatchmakingParams Parameters describing the search and the fallback host session.
	 * @param PolicyClass Optional custom matchmaking policy class. Uses the default policy when empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Start Easy Matchmaking", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "MatchmakingParams"))
	static UEasyMatchmakingNode* StartEasyMatchmaking(UObject* WorldContextObject, const FEasyMatchmakingParams& MatchmakingParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass = nullptr);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes matchmaking. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Parameters for the matchmaking run. */
	FEasyMatchmakingParams MatchmakingParams;

	/** Optional custom policy class. */
	UPROPERTY()
	TSubclassOf<UEasyMatchmakingPolicy> PolicyClass;
};
