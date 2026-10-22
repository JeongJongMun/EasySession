// Copyright (c) 2026 Langerak.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyCreatePartyNode.generated.h"

/**
 * Async node that creates a party, with the local player as its host.
 */
UCLASS()
class EASYSESSION_API UEasyCreatePartyNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the party was created. Get Easy Party Members already lists the host. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the party could not be created. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Create a party, with the local player as its host.
	 * A party lives outside game sessions and needs no map, so nothing travels.
	 * It fails with Session Already Exists in a game session or a party, so call Leave Easy Session for it first.
	 *
	 * @param PartySettings What the party advertises. Max Players counts the host.
	 *        A Password or Should Advertise off is refused with Invalid Params, because a party has no password and members find it again by its host.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Party", DisplayName = "Create Easy Party", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "PartySettings"))
	static UEasyCreatePartyNode* CreateEasyParty(UObject* WorldContextObject, const FEasySessionSettings& PartySettings);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the create request. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The settings to create the party with. */
	FEasySessionSettings PartySettings;
};
