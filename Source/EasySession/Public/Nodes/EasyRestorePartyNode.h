// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyRestorePartyNode.generated.h"

/**
 * Async node that brings back the party this player was in before its last game session.
 */
UCLASS()
class EASYSESSION_API UEasyRestorePartyNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the party is back. Get Easy Party Members already lists the local player. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the party did not come back. Has Easy Last Party stays true after No Sessions Found, so this can be called again. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Bring back the party this player was in before its last game session.
	 * Its host creates the party again, and a member searches for it and joins it.
	 * Call it in the menu after a match, for example when Has Easy Last Party is true.
	 *
	 * @param MaxSearchPasses How many times a member searches for the host's party, two seconds apart.
	 *        A member whose host is still in the match fails with No Sessions Found, and can call this again later.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Party", DisplayName = "Restore Easy Party", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UEasyRestorePartyNode* RestoreEasyParty(UObject* WorldContextObject, int32 MaxSearchPasses = 5);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the restore request. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** How many times a member searches for the host's party. */
	int32 MaxSearchPasses = 5;
};
