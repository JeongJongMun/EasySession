// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyEndSessionNode.generated.h"

/**
 * Async node that ends the match, transitioning the session back to Ended.
 */
UCLASS()
class EASYSESSION_API UEasyEndSessionNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was ended successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the session could not be ended. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * End the match: transitions the session back to Ended so a new match can be started.
	 *
	 * Only the game hosting the session can end the match - clients get a
	 * Requires Session Authority failure. Gate the button with Is Easy Session Host.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "End Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEasyEndSessionNode* EndEasySession(UObject* WorldContextObject);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the end operation. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);
};
