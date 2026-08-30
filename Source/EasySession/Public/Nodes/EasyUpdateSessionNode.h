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
	 *
	 * Only the game hosting the session can update it - clients get a Requires Session Authority failure.
	 *
	 * Every field is applied as given, including Password.
	 * Pass settings from Get Easy Session Settings and change only what you mean to change.
	 * Otherwise the fields you left at their defaults overwrite the session with those defaults.
	 *
	 * @param NewSettings The settings to advertise in place of the current ones. Get Easy Session Settings hands you the current ones to edit.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Update Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "NewSettings"))
	static UEasyUpdateSessionNode* UpdateEasySession(UObject* WorldContextObject, const FEasySessionSettings& NewSettings);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the update operation. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** New parameters to advertise. */
	FEasySessionSettings NewSettings;
};
