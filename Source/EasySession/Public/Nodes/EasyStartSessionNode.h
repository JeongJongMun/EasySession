// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyStartSessionNode.generated.h"

/**
 * Async node that starts the match, transitioning the session to InProgress.
 */
UCLASS()
class EASYSESSION_API UEasyStartSessionNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was started successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the session could not be started. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Start the match: transitions the session to InProgress.
	 * When Allow Join In Progress is disabled, new players are refused from here until the match ends - except on Steam, which already refused them from the first join onwards.
	 *
	 * Only the game hosting the session can start the match - clients get a Requires Session Authority failure.
	 * Show the button only when Is Easy Session Authority is true.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Start Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEasyStartSessionNode* StartEasySession(UObject* WorldContextObject);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the start operation. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);
};
