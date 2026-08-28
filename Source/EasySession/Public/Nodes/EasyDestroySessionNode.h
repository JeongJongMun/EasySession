// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyDestroySessionNode.generated.h"

/**
 * Async node that destroys the current session (IOnlineSession::DestroySession).
 */
UCLASS()
class EASYSESSION_API UEasyDestroySessionNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was destroyed successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the session could not be destroyed. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Destroy the named session: the host closes the session, a client removes its own copy and stays on the current map.
	 * A client leaving for the menu is Leave Easy Session; sending everyone back with a reason is Destroy Easy Session For Everyone on the subsystem.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Destroy Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEasyDestroySessionNode* DestroyEasySession(UObject* WorldContextObject);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the destroy operation. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);
};
