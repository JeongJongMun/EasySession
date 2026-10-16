// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyFindSessionsNode.generated.h"

/**
 * Async node that searches for sessions of one type: game sessions or party sessions.
 */
UCLASS()
class EASYSESSION_API UEasyFindSessionsNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called with the filtered results when the search completed successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionFindEvent OnSuccess;

	/** Called when the search failed. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionFindEvent OnFailure;

	/**
	 * Search for sessions of one type matching the given filters.
	 * A party search lists the parties that are not hidden, and with a Join Code it finds the party that advertises it, hidden or not.
	 * For a party, a result's Host Name is the leader's name.
	 * Its Session Display Name is the party's name, which is the leader's name unless the leader set one.
	 *
	 * @param Type The game sessions or the party sessions to search for.
	 *        A party advertises no region, custom settings or session state.
	 *        A party search ignores Region, Required Custom Settings and Include In Progress Sessions.
	 * @param SearchParams Parameters describing what to search for.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Find Easy Sessions", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "SearchParams"))
	static UEasyFindSessionsNode* FindEasySessions(UObject* WorldContextObject, EEasySessionType Type, const FEasySessionSearchParams& SearchParams);

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the search. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage, const TArray<FEasySessionSearchResult>& Results);

	/** The type of session to search for. */
	EEasySessionType Type = EEasySessionType::GameSession;

	/** Parameters to search with. */
	FEasySessionSearchParams SearchParams;
};
