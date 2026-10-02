// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyCreatePartyNode.generated.h"

/**
 * Async node that creates a party, with the local player as its leader.
 */
UCLASS()
class EASYSESSION_API UEasyCreatePartyNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the party was created. Get Easy Party Members already lists the leader. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the party could not be created. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Create a party, with the local player as its leader.
	 * A party lives outside game sessions and needs no map, so nothing travels.
	 * It fails inside a game session, and in a party, which Leave Easy Party ends first.
	 *
	 * @param PartySettings How many players the party holds, and who may join it.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Party", DisplayName = "Create Easy Party", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "PartySettings"))
	static UEasyCreatePartyNode* CreateEasyParty(UObject* WorldContextObject, const FEasyPartySettings& PartySettings);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the create request. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** Parameters to create the party with. */
	FEasyPartySettings PartySettings;
};
