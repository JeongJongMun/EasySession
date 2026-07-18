// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyCreateSessionNode.generated.h"

/**
 * Async node that creates a new session.
 */
UCLASS()
class EASYSESSION_API UEasyCreateSessionNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was created successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the session could not be created. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Create a new session and optionally travel to the session map.
	 * For listen servers the map is opened with the ?listen option automatically.
	 *
	 * @param HostParams Parameters describing the session to create.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Create Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "HostParams"))
	static UEasyCreateSessionNode* CreateEasySession(UObject* WorldContextObject, const FEasySessionHostParams& HostParams);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the create operation. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Parameters to create the session with. */
	FEasySessionHostParams HostParams;
};
