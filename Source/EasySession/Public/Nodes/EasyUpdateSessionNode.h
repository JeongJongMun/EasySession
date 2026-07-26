// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyUpdateSessionNode.generated.h"

/**
 * Async node that updates the advertised properties of the current session.
 */
UCLASS()
class EASYSESSION_API UEasyUpdateSessionNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was updated successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the session could not be updated. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Update the advertised properties of the current session.
	 * Only the host can update the session.
	 *
	 * @param NewHostParams New parameters to advertise. Map Name and Host Mode are ignored.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Update Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "NewHostParams"))
	static UEasyUpdateSessionNode* UpdateEasySession(UObject* WorldContextObject, const FEasySessionHostParams& NewHostParams);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the update operation. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** New parameters to advertise. */
	FEasySessionHostParams NewHostParams;
};
