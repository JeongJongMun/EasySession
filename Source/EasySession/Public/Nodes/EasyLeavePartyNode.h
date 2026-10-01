// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyLeavePartyNode.generated.h"

/**
 * Async node that leaves the party.
 */
UCLASS()
class EASYSESSION_API UEasyLeavePartyNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the party was left. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when there was no party to leave, or the party session could not be destroyed. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Leave the party.
	 * A leader who leaves ends the party for every member.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Party", DisplayName = "Leave Easy Party", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEasyLeavePartyNode* LeaveEasyParty(UObject* WorldContextObject);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the leave request. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);
};
