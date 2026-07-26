// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/EasySessionNodeBase.h"
#include "EasySessionSubsystem.h"
#include "EasyJoinSessionNode.generated.h"

/**
 * Async node that joins a session found by a search.
 */
UCLASS()
class EASYSESSION_API UEasyJoinSessionNode : public UEasySessionNodeBase
{
	GENERATED_BODY()

public:

	/** Called when the session was joined successfully. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnSuccess;

	/** Called when the session could not be joined. */
	UPROPERTY(BlueprintAssignable)
	FEasySessionEvent OnFailure;

	/**
	 * Join the given session and optionally travel to the host.
	 *
	 * @param SearchResult A search result returned by Find Easy Sessions.
	 * @param bTravelOnSuccess Whether to travel to the host once joined.
	 * @param Password Password for password protected sessions (see Password Protected on the search result).
	 * @param AdditionalTravelOptions Extra options appended to the travel URL (e.g. "Team=1?MyOption=2").
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession", DisplayName = "Join Easy Session", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "SearchResult", AdvancedDisplay = "Password,AdditionalTravelOptions"))
	static UEasyJoinSessionNode* JoinEasySession(UObject* WorldContextObject, const FEasySessionSearchResult& SearchResult, bool bTravelOnSuccess = true, const FString& Password = TEXT(""), const FString& AdditionalTravelOptions = TEXT(""));

	//~ Begin UBlueprintAsyncActionBase Interface
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase Interface

private:

	/** Called when the subsystem finishes the join operation. */
	void HandleComplete(EEasySessionResult Result, const FString& ErrorMessage);

	/** The session to join. */
	FEasySessionSearchResult SearchResult;

	/** Whether to travel to the host once joined. */
	bool bTravelOnSuccess = true;

	/** Password for password protected sessions. */
	FString Password;

	/** Extra options appended to the travel URL. */
	FString AdditionalTravelOptions;
};
