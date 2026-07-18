// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyLeaveSessionNode.generated.h"

/**
 * Async node that leaves and destroys the current session.
 */
UCLASS()
class EASYSESSION_API UEasyLeaveSessionNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was left successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the session could not be left. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Leave the current session. As the host this destroys the session for everyone,
	 * as a client this only leaves it.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Leave Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEasyLeaveSessionNode* LeaveEasySession(UObject* WorldContextObject);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the destroy operation. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);
};
