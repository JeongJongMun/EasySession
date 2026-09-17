// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyLeaveSessionNode.generated.h"

/**
 * Async node that leaves the session: destroys this game's named session, then travels to the menu map.
 */
UCLASS()
class EASYSESSION_API UEasyLeaveSessionNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was left successfully. The menu travel has already started. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the session could not be destroyed. The menu travel has started regardless. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Leave the session: destroy this game's named session, then travel to the menu map (Game Default Map).
	 * A leaving host destroys the session for everyone, and each client receives "The host has left the game." first.
	 * For a custom reason use Destroy Easy Session For Everyone on the subsystem.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Leave Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEasyLeaveSessionNode* LeaveEasySession(UObject* WorldContextObject);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the leave operation. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);
};
