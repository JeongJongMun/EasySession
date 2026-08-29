// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyJoinSessionByCodeNode.generated.h"

/**
 * Async node that finds the session advertising a join code and joins it.
 */
UCLASS()
class EASYSESSION_API UEasyJoinSessionByCodeNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was joined successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when no session advertises the code, or it could not be joined. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Find the session advertising this join code and join it. Hidden sessions count - a code names one exact room.
	 *
	 * @param JoinCode The code the host reads from Get Easy Session Join Code. Case does not matter.
	 * @param Password Password for password protected sessions - a code identifies the room, the password still protects it.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Join Easy Session By Code", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AdvancedDisplay = "Password"))
	static UEasyJoinSessionByCodeNode* JoinEasySessionByCode(UObject* WorldContextObject, const FString& JoinCode, const FString& Password = TEXT(""));

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the join operation. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The code to look for. */
	FString JoinCode;

	/** Password for password protected sessions. */
	FString Password;
};
