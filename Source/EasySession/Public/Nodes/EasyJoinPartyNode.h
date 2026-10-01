// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyJoinPartyNode.generated.h"

/**
 * Async node that joins a party found by a search.
 */
UCLASS()
class EASYSESSION_API UEasyJoinPartyNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the party was joined. Get Easy Party Members already lists the local player. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the party could not be joined. A refusal comes with the leader's reason. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Join a party found by Find Easy Parties.
	 * The leader decides the join, and a refusal fails with Join Refused and the leader's reason.
	 * A player in a party or in a game session is refused with Session Already Exists.
	 *
	 * @param SearchResult A party returned by Find Easy Parties.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Party", DisplayName = "Join Easy Party", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "SearchResult"))
	static UEasyJoinPartyNode* JoinEasyParty(UObject* WorldContextObject, const FEasySessionSearchResult& SearchResult);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the join request. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The party to join. */
	FEasySessionSearchResult SearchResult;
};
