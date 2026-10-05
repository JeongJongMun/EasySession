// Copyright (c) 2026 Langerak. Licensed under the MIT License.

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
	 * Create a new session and travel to Initial Map Name.
	 * The map is opened with the ?listen option, which is what starts the listen server.
	 * A party leader brings the party, whose members follow once the map is open, and a party member fails with In Party.
	 *
	 * @param HostParams Parameters describing the session to create.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Create Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "HostParams"))
	static UEasyCreateSessionNode* CreateEasySession(UObject* WorldContextObject, const FEasySessionHostParams& HostParams);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the create request. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Parameters to create the session with. */
	FEasySessionHostParams HostParams;
};
